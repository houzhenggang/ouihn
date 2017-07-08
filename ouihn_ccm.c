/************************************************************************************************************
 * ouihn_cong program
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 * Description
 *  ouihnにおける輻輳制御関連のプログラムを定義する
 *  
 *  輻輳検知フェーズ、輻輳監視フェーズ、輻輳回避フェーズの違いは、以下である
 *
 *  輻輳検知: 輻輳検知インターバルに基づいて、各デバイスのスループットを取得し記録する。
 *            輻輳検知アルゴリズムにより算出される輻輳スループットが設定した輻輳閾値を下回った場合
 *            輻輳検知フェーズに移行する
 *
 *  輻輳監視: 輻輳監視インターバルに基づいて、各デバイスのスループットを取得し記録する。
 *            輻輳監視タイマが切れるまで、輻輳が持続した場合、輻輳回避フェーズに移行する
 *
 *  輻輳回避: 輻輳回避インターバルに基づいて、各デバイスの接続を切断する。
 *            切断する順序は、CPが低い順で行い切断後、輻輳回避インターバルに基づいてスループットの
 *            取得、記録を行う。スループットが輻輳スループット以上に回復した場合、輻輳監視フェーズに回帰する。
 *  
 ************************************************************************************************************/

#include "ouihn_ccm.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


//輻輳しているか判定するマクロ.輻輳なら1 輻輳ではないなら0を返す
#define isCongestion() (\
                        (sg_cong_th < sg_cong_threshold) && \
                        (sg_ch_cong_flag == 1) && \
                        (sg_min_num_com_dev < sg_num_com_dev)\
                       )? (1) : (0)

int g_phase_status = DETECT;                    //輻輳検知フェーズ:0 輻輳監視フェーズ:1、輻輳回避フェーズ:2

static int sg_mn_inval,sg_dt_inval,sg_av_inval;  //監視、検知、回避インターバル. 終了待機時間以下でなければならない
static int sg_mn_sec;              		         //輻輳監視フェーズで監視する時間

//輻輳検知アルゴリズムに使用する変数
static double sg_cong_threshold;        //輻輳閾値
static double sg_cong_th;               //輻輳スループット

static int sg_ch_util;                  //チャネル使用率
static int sg_ch_cong_flag;             //チャネル使用率が閾値を超えたらフラグを立てる

static int sg_num_com_dev;              //通信している台数
static int sg_min_num_com_dev;          //最小通信台数

static void congAvoid(void);
static int congMonitor(void);
static void ctlInit(void);
static void ctlSet(int *ctl_sum);
static int getCutClient(int ctl_sum);
static double getThr(char *mac_addr,int cdb_no);
static void *setThrThread(void *arg);
static void *surveyThread(void *arg);

//輻輳制御スレッド
//getThroughputとisCongestionの機能を切り離す必要がある
void *congestionCtrl(void *arg)
{
    char filename[64];
    pthread_t thr_th_id,survey_th_id;

    pthread_detach(pthread_self());

    dprint(MSG_PRIO_LOW,"<<<<<  Start Congestion Control Thread  >>>>>\n");

    sg_min_num_com_dev = atoi(getConfig("MIN_NUM_COM_DEV"));
    sg_dt_inval = atoi(getConfig("COG_DT_INVAL"));
    sg_mn_inval = atoi(getConfig("COG_MN_INVAL"));
    sg_av_inval = atoi(getConfig("COG_AV_INVAL"));

    sg_mn_sec = atoi(getConfig("MN_SEC"));

    sg_cong_threshold = atof(getConfig("COG_THRESH"));

    //setThroughput threadを起動する
    //CDBに接続している各端末のスループットを取得する
    if(pthread_create(&thr_th_id,NULL,setThrThread,NULL) != 0){
        perrExit("pthread_create",__FILE__,__LINE__);
    }
   
    //setChUtil threadを起動する
    //チャネル使用率を取得する
    if(pthread_create(&survey_th_id,NULL,surveyThread,NULL) != 0){
        perrExit("pthread_create",__FILE__,__LINE__);
    }

    for(;;){
        //輻輳検知フェーズ
        //輻輳検知インターバルは2秒程度がいいかも
        if(isCongestion()){
            //輻輳監視フェーズに移行する
            //優先制御を有効にする
            dprint(MSG_PRIO_HIGH,"<----------------------  Switch Monitoring Phase  ---------------------->\n");

            g_phase_status = MONITOR;

            if(congMonitor()){
                //輻輳回避フェーズに移行する
                dprint(MSG_PRIO_HIGH,"<----------------------  Switch Avoid Phase  ---------------------->\n");

                g_phase_status = AVOID;

                congAvoid();

                dprint(MSG_PRIO_HIGH,"<----------------------  Switch Detect Phase  ---------------------->\n");
            }   
            else{
                dprint(MSG_PRIO_HIGH,"<----------------------  Switch Detect Phase  ---------------------->\n");
            }
        }
        else{
            sleep(sg_dt_inval);
        }

        g_phase_status = DETECT;
    }
    
    dprint(MSG_PRIO_LOW,"<<<<<  End Congestion Control Thread  >>>>>\n");
}


//輻輳回避を行う
void congAvoid(void)
{
    int cdb_no,ctl_sum = 0;

    for(;g_cdb_mg.num_cons > 1;){
        //スループットが輻輳閾値を下回ったら戻る
        if(!isCongestion())
            return;    

        ctlSet(&ctl_sum);

        //デバッグモードで起動していたら各クライアントのCTLを出力する
         

        //CTLとCPの値から切断するクライアントを決定し切断する
        pthread_mutex_lock(&g_cdb_mg.cut_permit_mtx);

        cdb_no = getCutClient(ctl_sum);
        //エラーの場合
        if(cdb_no == -1){ 
            pthread_mutex_unlock(&g_cdb_mg.cut_permit_mtx);
            continue;
        }

        Cdb_No.will_cut_flag = 1;
        pthread_mutex_unlock(&g_cdb_mg.cut_permit_mtx);

        conCut(cdb_no,"Avoid",1); 

        ctlInit();
        sleep(sg_av_inval);
    } 
}

//輻輳監視を行う
int congMonitor(void)
{
    int mn_cnt;
    int cong_occur = sg_mn_sec / sg_mn_inval;

    //各クライアントのCTLを初期化する
    ctlInit();

    //mn_invalの間隔でスループットを測定し、輻輳状態を監視する
    for(mn_cnt = 0;mn_cnt < cong_occur;mn_cnt++){
        if(!isCongestion())
            return 0;

        sleep(sg_mn_inval);
    }

    return 1;
}

//各クライアントのbase_ctlを初期化する
void ctlInit(void)
{
    int cdb_no;

    for(cdb_no = 0;cdb_no < g_max_client;cdb_no++){
        if(Cdb_No.st == CONNECTED && Cdb_No.will_cut_flag == 0){
            Cdb_No.tx_base_ctl = Cdb_No.now_tx_bytes;
			Cdb_No.rx_base_ctl = Cdb_No.now_rx_bytes;
            Cdb_No.ctl = 0;
        }
    }
}

//各クライアントのCTLをセットする
//切断処理は競合プロセスが多いため、なるべくg_cdb_mg.cut_permit_mtxをロックする時間を短くする必要がある
//そのためこのようにCTLの合計、平均を求める処理を別に分けた
//ただし着目クライアントの生存をちゃんと確認すること
void ctlSet(int *ctl_sum)
{
    int cdb_no,add_cnt = 0;

    for(cdb_no = 0;cdb_no < g_max_client;cdb_no++){
        if(Cdb_No.st == CONNECTED && Cdb_No.will_cut_flag == 0 && Cdb_No.tx_base_ctl != -1 && Cdb_No.rx_base_ctl != -1){
            Cdb_No.ctl = Cdb_No.now_tx_bytes - Cdb_No.tx_base_ctl + Cdb_No.now_rx_bytes - Cdb_No.rx_base_ctl;
            *ctl_sum += Cdb_No.ctl;
            add_cnt++;
        }
    }
}

//切断するクライアントを決定する
int getCutClient(int ctl_sum)
{
    int cdb_no,max_no = -1;
    double max_cv = 0.0,cv,x;
    char buf[4096],buf2[64];

    for(cdb_no = 0;cdb_no < g_max_client;cdb_no++){
        if(Cdb_No.st == CONNECTED && Cdb_No.will_cut_flag == 0){
            x = (double)Cdb_No.ctl / ctl_sum;
            if(x >= 0.3)
                x = 0.1;
            else if(x < 0.3 && x >= 0.2)
                x = 0.25;
            else if(x < 0.2 && x >= 0.1)
                x = 0.5;
            else if(x < 0.1 && x >= 0.05)
                x = 0.8;
            else if(x < 0.05 && x >= 0.03)
                x = 0.9;
            else if(x < 0.03 && x >= 0.01)
                x = 1.1;
            else
                x = 2.1;

            cv = (double)(Cdb_No.ctl * 100) / (ctl_sum * Cdb_No.cp * x);
            if(max_no == -1 || max_cv < cv){
                max_cv = cv;
                max_no = cdb_no;
            }
            
            //デバッグモードならctlとcvを出力する
            //処理的に重いので、デバッグモードがオフの時は処理をしない
            if(g_debug_mode){
                snprintf(buf2,sizeof(buf2),"%s/%ld/%.5f, ",PRINTNAME,Cdb_No.ctl,cv);
                strncat(buf,buf2,sizeof(buf2));
            }
        }    
    }
    if(g_debug_mode)
        dprint(MSG_PRIO_LOW,"{ %s }\n",buf);

    return max_no;
}

//各デバイスのスループットを取得する
//エラーのときは-1.0を返す
//最初の呼び出しのときは0.0を返す
double getThr(char *mac_addr,int cdb_no)
{
    double temp_th = -1.0,div_time;
    FILE *fp;
    int i;
    long int tx_diff,rx_diff;
    char command[64],tx_bytes[16],rx_bytes[16],buf[64],*p;
    struct timeval now_time;

    //station getでそのデバイスのtx_byteを取得
    snprintf(command,sizeof(command),STATION_GET,mac_addr);
    if((fp = popen(command,"r")) == NULL){
        perr("station get failure",__FILE__,__LINE__);
        return -1.0;
    }
   
    gettimeofday(&now_time,NULL);
    //read result and edit anything
    while(fgets(buf,sizeof(buf),fp) != NULL){
        if(strstr(buf,"rx bytes") != NULL){
            for(p = buf;!isdigit(*p);p++)
                ;

            for(i = 0;isdigit(*p);p++,i++)
                rx_bytes[i] = *p;
            rx_bytes[i] = '\0';
            
            //スループットの計測
            //最初の計測は不可能なので2回目の計測から行う
            Cdb_No.now_rx_bytes = atol(rx_bytes);
        }
        if(strstr(buf,"tx bytes") != NULL){
            for(p = buf;!isdigit(*p);p++)
                ;

            for(i = 0;isdigit(*p);p++,i++)
                tx_bytes[i] = *p;
            tx_bytes[i] = '\0';
            
            //スループットの計測
            //最初の計測は不可能なので2回目の計測から行う
            Cdb_No.now_tx_bytes = atol(tx_bytes);

            break;
        }
    }
    
    //初めの取得では0を返す
    if(Cdb_No.prev_measure_time.tv_sec == 0){
        Cdb_No.prev_rx_bytes = Cdb_No.now_rx_bytes;
        Cdb_No.prev_tx_bytes = Cdb_No.now_tx_bytes;
        Cdb_No.prev_measure_time = now_time;

        return 0.0;
    }

    
    //前回計測時間と今の時間の差を使って計測する
    div_time = (double)(now_time.tv_sec - Cdb_No.prev_measure_time.tv_sec);
    div_time += (double)((now_time.tv_usec - Cdb_No.prev_measure_time.tv_usec) * 0.000001);

    //tx_bytesがサイクルした
    tx_diff = rx_diff = -1;
    if(Cdb_No.now_tx_bytes < Cdb_No.prev_tx_bytes){
        tx_diff = UINT_MAX - Cdb_No.prev_tx_bytes;
    }

    if(Cdb_No.now_rx_bytes < Cdb_No.prev_rx_bytes){
        rx_diff = UINT_MAX - Cdb_No.prev_rx_bytes;
    }
   
    if(tx_diff != -1 && rx_diff != -1){
        temp_th = (double)(Cdb_No.now_tx_bytes + tx_diff + Cdb_No.now_rx_bytes + rx_diff) / div_time;
    }
    else if(tx_diff != -1){
        temp_th = (double)(Cdb_No.now_tx_bytes + tx_diff + Cdb_No.now_rx_bytes - Cdb_No.prev_rx_bytes) / div_time;
    }
    else if(rx_diff != -1){
        temp_th = (double)(Cdb_No.now_tx_bytes - Cdb_No.prev_tx_bytes + rx_diff + Cdb_No.now_rx_bytes) / div_time;
    }
    else{
        temp_th = (double)(Cdb_No.now_tx_bytes - Cdb_No.prev_tx_bytes + Cdb_No.now_rx_bytes - Cdb_No.prev_rx_bytes) / div_time;
    }

    //ビットに単位を変換
    temp_th *= 8;

    Cdb_No.prev_tx_bytes = Cdb_No.now_tx_bytes;
    Cdb_No.prev_rx_bytes = Cdb_No.now_rx_bytes;
    Cdb_No.prev_measure_time = now_time;

    pclose(fp);

    //エラー値の場合はスループットを更新しない
    if(temp_th > 50000000.0 || temp_th < 0.0){
        dprint(MSG_PRIO_LOW,"throughput error value %s: %lf\n",PRINTNAME,temp_th);
        return -1.0;
    }
    else{
        return temp_th;
    }
}

//スループットをセットするスレッド
void *setThrThread(void *arg)
{
    int i,add_cnt,cdb_no;
    int inval;
    char buf[64],filename[64],*p; 
    double temp_th,temp_cong_th,ret;
    struct timeval now_time;
    FILE *fp,*cong_th_fp;
   
    pthread_detach(pthread_self());
   
    inval = atoi(getConfig("THR_GET_INVAL"));
    
    //データを出力するファイルを用意する
    snprintf(filename,sizeof(filename),"%s/%s",g_workspace_name,"cong_th");
    cong_th_fp = fopen(filename,"w");
    if(cong_th_fp == NULL){
        perrExit("cong_th_fp open error",__FILE__,__LINE__);
    }
    //バッファリングを行わないようにする
    setbuf(cong_th_fp,NULL);

    for(;;){
        add_cnt = 0;
        temp_cong_th = 0.0;
        gettimeofday(&now_time,NULL);
        
        for(cdb_no = 0;cdb_no < g_max_client;cdb_no++){
            //ここの処理は時間がかかるためノンブロッキングにするかwill_cut_flagをチェックするなど
            //工夫の余地がある
            pthread_mutex_lock(&Cdb_No.con_dev_mtx);
            if(Cdb_No.st == CONNECTED){
                //エラーの場合は、値を変更しない
                if((ret =  getThr(Cdb_No.mac_addr,cdb_no)) != -1.0){
                    Cdb_No.throughput = ret;
                    temp_cong_th += Cdb_No.throughput;

                    //初めの取得では0を返すがその時は分母に加えない
                    if(Cdb_No.throughput > 1.0)
                        add_cnt++;
                }
            }
            pthread_mutex_unlock(&Cdb_No.con_dev_mtx);
        }
    
        //輻輳スループットを算出する
        if(add_cnt == 0){
            sg_cong_th = 20000000.0;    //APに１台も接続してない時のおおよその速度
        }
        else{
            sg_cong_th = temp_cong_th / add_cnt;
            dprint2(cong_th_fp,"%d %d %d\n",\
                    (int)sg_cong_th,sg_ch_cong_flag,(int)sg_cong_threshold);
        }

        sg_num_com_dev = add_cnt;

        sleep(inval);
    }

    fclose(cong_th_fp);
}

//チャネル使用率とノイズ値をセットするスレッド
void *surveyThread(void *arg)
{
    int i,active_time,busy_time;
    int inval,ch_util_thresh;
    char buf[64],*p; 
    char atime[16],btime[16],noise[4];
    FILE *fp;
   
    pthread_detach(pthread_self());

    inval = atoi(getConfig("SURVEY_INVAL"));
    ch_util_thresh = atoi(getConfig("CH_UTIL_THRESH"));

    for(;;){
        if((fp = popen(SURVEY_DUMP,"r")) == NULL){
            perr("survey dump failure",__FILE__,__LINE__);
            goto SLEEP;
        }

        //最初の行を読み飛ばし
        //survey dumpコマンドの出力をよく確認すること
        fgets(buf,sizeof(buf),fp);
        fgets(buf,sizeof(buf),fp);

        //active_timeを読み取る
        fgets(buf,sizeof(buf),fp);
        for(p = buf;!isdigit(*p);p++)
            ;
        for(i = 0;isdigit(*p);p++,i++)
            atime[i] = *p;
        atime[i] = '\0';
        active_time = atoi(atime);

        //busy_timeを読み取る
        fgets(buf,sizeof(buf),fp);
        for(p = buf;!isdigit(*p);p++)
            ;
        for(i = 0;isdigit(*p);p++,i++)
            btime[i] = *p;
        btime[i] = '\0';
        busy_time = atoi(btime);

        sg_ch_util = (busy_time * 100) / active_time;

        if(ch_util_thresh > sg_ch_util){
            sg_ch_cong_flag = 0;
        }
        else{
            sg_ch_cong_flag = 1;
        }

SLEEP:
        pclose(fp);
        sleep(inval);
    }

    return (void *)0;
}
