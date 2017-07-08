/************************************************
 * ouihn connection control module
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 * Description
 *  接続制御モジュール
 ************************************************/

#include "ouihn_concm.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#define DISCONNECT '0'
#define CONNECT '1'
#define requestIsConnect() ( *p == CONNECT )
#define requestIsDisConnect() ( *p == DISCONNECT )
#define COUNTUP(type) do{ if(mon_dev_no != -1) g_mon_devs[mon_dev_no].counter[type]++; }while(0)

//関数プロトタイプ
static void cctInit(char *config_filename);
static void *cctRefreshThread(void *arg);
static int checkCct(char *mac_addr);
static void cleanupHandler(int cdb_no);
static int dupCheck(char *mac_addr);
static int getCdb_No(char *mac_addr);
static int getMonDevNo(char *mac_addr);
static int getRandCP(void);
static void recAccept(int cp,int rssi,int mon_dev_no,char *mac_addr);
static void recSwap(int cp,int rssi,int cdb_no,int mon_dev_no,char *mac_addr);
static void recReject(int cp,int rssi,int cdb_no,int mon_dev_no,char *mac_addr);
static int requestCheck(int cp,int rssi);
static void registerToCct(char *mac_addr,int cp);
static int searchMinCP(void);
static void setConCMRuleBase(char *config_filename);

static int sg_rfifo_fd, sg_wfifo_fd;    //FIFOのファイルディスクリプタ
static struct cutClient *sg_cct;        //cut client table
static int sg_num_cct_ent;              //number of cct entry
static int sg_BRT;                      //Basic Refresh Time
static int sg_PRT[5];					//Prioritized Refresh Time
static int sg_concm_rule[2][3][5];      //接続制御用ルールベース

//hostapdから接続情報を読み込み各デバイスの接続制御を行う
void *connectionCtrl(void *arg)
{
    int i,cp,rssi;
    int no_reg_dev,no_reg_dev_cp;
    int mon_dev_no,cdb_no,rlt;
    char cl_name[20],buf[64],mac_addr[MAC_ADDR_LEN],command[64];
    char *p;
	pthread_t cct_refresh_th_id;

    dprint(MSG_PRIO_LOW,"<<<<<  Start Connection Control Thread  >>>>>\n");

    //cctを初期化する 
    cctInit((char *)arg);

    //ルールベースを初期化する
    setConCMRuleBase((char *)arg);

    no_reg_dev = atoi(getConfig("NO_REG_DEV"));
    if(no_reg_dev == 1)
        no_reg_dev_cp = atoi(getConfig("NO_REG_DEV_CP"));
    else if(no_reg_dev == 2)
        srand((unsigned)time(NULL));

    //cctをリフレッシュするスレッドを起動する
    if(pthread_create(&cct_refresh_th_id,NULL,cctRefreshThread,NULL) != 0){
        perrExit("pthread_create",__FILE__,__LINE__);
    }

    //hostapdにメッセージを送信するFIFOをオープンする
    sg_wfifo_fd = open(FIFOPATH2,O_WRONLY);
    if(sg_wfifo_fd == -1){
        perrExit("FIFO open",__FILE__,__LINE__);
    }

    //hostapdからメッセージを受信するFIFOをオープンする
    sg_rfifo_fd = open(FIFOPATH,O_RDONLY);
    if(sg_rfifo_fd == -1){
        perrExit("FIFO open",__FILE__,__LINE__);
    }
   
    //FIFOに対するSIGPIPEを無視する
    if(signal(SIGPIPE,SIG_IGN) == SIG_ERR)
        perrExit("signal",__FILE__,__LINE__);


    for(;;){
        //読み込み
        if(read(sg_rfifo_fd,buf,19) != 19){
            perrExit("read error",__FILE__,__LINE__);
        }

        //MACアドレス読み込み
        for(i = 0,p = buf;*p != ',';i++,p++)
            mac_addr[i] = *p;
        mac_addr[i] = '\0';
        p++;

        //CONNECTEDなら輻輳フラグを調べて、Accept or Swap or Reject
        //Classification
        if( requestIsConnect() ){
            //重複リクエストチェック
            if( (cdb_no = dupCheck(mac_addr)) >= 0 ){
                //クライアントが切断したことに気づかなかった場合
                //CDBから削除する
                dprint(MSG_PRIO_LOW,"%s DISCONNECT fault\n",PRINTNAME);

                pthread_mutex_lock(&Cdb_No.con_dev_mtx);
                cleanupHandler(cdb_no); 
                pthread_mutex_unlock(&Cdb_No.con_dev_mtx);
            }
            else if(cdb_no == -1){
                //重複リクエスト 
                continue;
            }

            //cctをチェックする
            if(checkCct(mac_addr) == REJECT){
                //短時間での再接続
                //Rejectする
                dprint(MSG_PRIO_HIGH,"%s exist at cct\n",mac_addr);

                write(sg_wfifo_fd,(void *)mac_addr,17);
                continue;
            }

            //リクエストしてきたデバイスの特定
            mon_dev_no = getMonDevNo(mac_addr);
            if(mon_dev_no == -1){
                //監視対象デバイスではない
                dprint(MSG_PRIO_LOW,"%s Request received\n",mac_addr);
                
                if(no_reg_dev == 0){
                    //NO_REG_DEVが0にセットされてたらREJECTする
                    dprint(MSG_PRIO_LOW,"%s  do not register device\n",mac_addr);

                    write(sg_wfifo_fd,(void *)mac_addr,17);
                    continue;
                }
                else if(no_reg_dev == 1){
                    cp = no_reg_dev_cp; 
                }
                else{   //2の場合は乱数を設定する
                    cp = getRandCP();
                }
            }
            else{
                dprint(MSG_PRIO_LOW,"%s Request received\n",g_mon_devs[mon_dev_no].dev_name);
                COUNTUP(REQUEST);
                cp = g_mon_devs[mon_dev_no].cp;
            }
           
            rssi = getRSSI(mac_addr);

            pthread_mutex_lock(&g_cdb_mg.num_con_mtx);
            //CDBの最大値を超えた場合
            if(g_cdb_mg.num_cons > g_max_client){
                pthread_mutex_unlock(&g_cdb_mg.num_con_mtx);
                dprint(MSG_PRIO_HIGH,"CDB is too many connections\n");
                goto SWAP;
            }
            pthread_mutex_unlock(&g_cdb_mg.num_con_mtx);

            if( (rlt = requestCheck(cp,rssi)) == ACCEPT ){
                COUNTUP(ACCEPT); 
                recAccept(cp,rssi,mon_dev_no,mac_addr);
            }
            else if(rlt == SWAP){   
SWAP:
                pthread_mutex_lock(&g_cdb_mg.cut_permit_mtx);
                cdb_no = searchMinCP();      //CDBから最小のCPを探索する
                if(cdb_no == -1){
                    pthread_mutex_unlock(&g_cdb_mg.cut_permit_mtx);
                    
                    //CDBに接続されていないのにDETECTフェーズに移行しない
                    dprint(MSG_PRIO_LOW,"CDB nothing access device\n");
                    COUNTUP(ACCEPT); 
                    recAccept(cp,rssi,mon_dev_no,mac_addr);
                    continue;
                }
                
                Cdb_No.will_cut_flag = 1;
                pthread_mutex_unlock(&g_cdb_mg.cut_permit_mtx);
                COUNTUP(SWAP);

                //接続を交換する
                recSwap(cp,rssi,cdb_no,mon_dev_no,mac_addr);
				

            }
            else{
                COUNTUP(REJECT);
                pthread_mutex_lock(&g_cdb_mg.cut_permit_mtx);

				cdb_no = searchMinCP();      //CDBから最小のCPを探索する
				recReject(cp,rssi,cdb_no,mon_dev_no,mac_addr);
				
				pthread_mutex_unlock(&g_cdb_mg.cut_permit_mtx);
            }

        }
        else if( requestIsDisConnect() ){
            //DISCONNECTEDなら監視デバイスか調べてデータを保存する
            
            //MACアドレスからcdb_noを求める
            cdb_no = getCdb_No(mac_addr);
            if(cdb_no == -1){
                dprint(MSG_PRIO_LOW,"%s does not already exist\n",mac_addr);
                continue;
            }

            dprint(MSG_PRIO_LOW,"%s connection shutdown\n",PRINTNAME);

            pthread_mutex_lock(&Cdb_No.con_dev_mtx);
            cleanupHandler(cdb_no); 
            pthread_mutex_unlock(&Cdb_No.con_dev_mtx);
        }
        else{
            dprint(MSG_PRIO_HIGH,"Request error: %s,%c\n",mac_addr,*p);
        }
    }
    
    close(sg_wfifo_fd);
    close(sg_rfifo_fd);
    free(sg_cct);

    dprint(MSG_PRIO_LOW,"<<<<<  End Connection Control Thread  >>>>>\n");

    return (void *)0;
}

//cctの設定を初期化する
void cctInit(char *config_filename)
{
    char filename[64],st[64],*val;
    FILE *fp;
    int i;

    sg_BRT = atoi(getConfig("BRT"));
    
    //PRTを設定する
	snprintf(filename,sizeof(filename),"%s/%s",g_workspace_name,config_filename);
    fp = fopen(filename,"r");
    if(fp == NULL){
        perrExit("fopen",__FILE__,__LINE__);
    }

    while(noLineFgets(st,sizeof(st),fp) != NULL){
        if(!strcmp(st,"[PRT]"))
            break;
    }
    if(feof(fp) || strcmp(st,"[PRT]")){
        errExit("You have to write [PRT] field",__FILE__,__LINE__);
    }
    //[PRT]の次の行はコメントや空行ではない前提で
    noLineFgets(st,sizeof(st),fp);

    i = 0;
    val = strtok(st,",");
    sg_PRT[i] = atoi(val);
    i++;
    while(val != NULL && i < 5){
        val = strtok(NULL,",");
        if(val != NULL)
            sg_PRT[i] = atoi(val);

        i++;
    }
    fclose(fp);

    //cctを確保する
    sg_num_cct_ent = atoi(getConfig("NUM_CCT_ENT"));
    sg_cct = (struct cutClient *)Malloc(sizeof(struct cutClient)  * sg_num_cct_ent);
    for(i = 0;i < sg_num_cct_ent;i++){
        sg_cct[i].st = EMPTY;
        sg_cct[i].mac_addr[0] = '\0';
        sg_cct[i].cut_time.tv_sec = (time_t)0;
    }

}

//cctをリフレッシュするスレッド
void *cctRefreshThread(void *arg)
{
    int i;
    struct timeval now_time;

    pthread_detach(pthread_self());

    for(;;){
        gettimeofday(&now_time,NULL);

        for(i = 0;i < sg_num_cct_ent;i++){
            if(sg_cct[i].st == USED && (now_time.tv_sec - sg_cct[i].cut_time.tv_sec) > (time_t)sg_cct[i].ref_time){
                //refresh
                sg_cct[i].mac_addr[0] = '\0';
                sg_cct[i].ref_time = 0;
                sg_cct[i].cut_time.tv_sec = (time_t)0;
                sg_cct[i].st = EMPTY;
            }
        }
        
        sleep(5);
    }

    return (void *)0;
}

//cctに存在するかチェックする
int checkCct(char *mac_addr)
{
    int i;

    for(i = 0;i < sg_num_cct_ent;i++){
        if(sg_cct[i].st == USED && !strcmp(mac_addr,sg_cct[i].mac_addr)){
            return REJECT;
        }
    }
    return ACCEPT;
}

//デバイスを切断する際の後始末を行う
void cleanupHandler(int cdb_no)
{
    struct timeval closed_time;
    time_t tim;
    
    Cdb_No.st = CLOSED1;

    dprint(MSG_PRIO_LOW,"#### Start CleanupHandler #### %s will_cut_flag = %d\n",PRINTNAME,Cdb_No.will_cut_flag);

    //監視デバイスでなければ、CDBの初期化だけでいい
    if(Cdb_No.dev_no == -1){
        goto CDB_NO_INIT;
    }

    gettimeofday(&closed_time,NULL);

    fprintf(g_mon_devs[Cdb_No.dev_no].ctime_fp,"%ld - %ld\n",\
                    (long)(Cdb_No.connected_time.tv_sec - g_start_time.tv_sec),\
                    (long)(closed_time.tv_sec - g_start_time.tv_sec)); 

    tim = (closed_time.tv_sec - Cdb_No.connected_time.tv_sec);

    g_mon_devs[Cdb_No.dev_no].total_time += tim; 
   
    g_mon_devs[Cdb_No.dev_no].cdb_no = -1;
	

CDB_NO_INIT:
    g_cdb_mg.nc_cp[Cdb_No.cp-1]--;
    Cdb_No.mac_addr[0] = '\0';
    Cdb_No.cp = 0;
    Cdb_No.rssi = 0;
    Cdb_No.cond_cnt = 0;
    Cdb_No.tx_base_ctl = -1;
	Cdb_No.rx_base_ctl = -1;
    Cdb_No.ctl = 0;
    Cdb_No.now_tx_bytes = 0;
    Cdb_No.prev_tx_bytes = 0;
	Cdb_No.now_rx_bytes = 0;
	Cdb_No.prev_rx_bytes = 0;
    Cdb_No.prev_measure_time.tv_sec = (time_t)0;
    Cdb_No.throughput = 0.0;
    Cdb_No.connected_time.tv_sec = (time_t)0;
    Cdb_No.will_cut_flag = 0;

    Cdb_No.dev_no = -1;

    pthread_mutex_lock(&g_cdb_mg.num_con_mtx);
    g_cdb_mg.num_cons--;
    pthread_mutex_unlock(&g_cdb_mg.num_con_mtx);
   
    Cdb_No.st = CLOSED2;
    
    dprint(MSG_PRIO_LOW,"#### End CleanupHandler ####\n");
}

//接続を切断する関数
//reasonには切断理由の文字列を渡す。reg_flagはcctに登録するなら0以外を渡す
int conCut(int cdb_no,char *reason,int reg_flag)
{
    char command[64],cut_mac_addr[MAC_ADDR_LEN];

    pthread_mutex_lock(&Cdb_No.con_dev_mtx);
    if(Cdb_No.st == CONNECTED){
        dprint(MSG_PRIO_HIGH,"%s: %s connection cut\n",reason,PRINTNAME);
        g_mon_devs[Cdb_No.dev_no].counter[CUT]++;

        if(reg_flag){
            //cctに登録する
            registerToCct(Cdb_No.mac_addr,Cdb_No.cp);
        }

        //切断するデバイスのMACアドレスをコピー
        strcpy(cut_mac_addr,Cdb_No.mac_addr);
        
        //後始末
        cleanupHandler(cdb_no);

        write(sg_wfifo_fd,(void *)cut_mac_addr,17);
        //snprintf(command,sizeof(command),STATION_DEL,cut_mac_addr);
        //iwのコマンドで切断する
        //system(command);

        //切断されたら音を出す
        system(BEEP);
        pthread_mutex_unlock(&Cdb_No.con_dev_mtx);
        return 0;
    }
    else{
        pthread_mutex_unlock(&Cdb_No.con_dev_mtx);
        return -1;
    }
}


//重複リクエストチェック
int dupCheck(char *mac_addr)
{
    int cdb_no;
    struct timeval now_time;

    gettimeofday(&now_time,NULL);

    cdb_no = getCdb_No(mac_addr);
    if(cdb_no != -1){
        //重複リクエストの場合
        if( (now_time.tv_sec - Cdb_No.connected_time.tv_sec) > 90 ){
            //クライアントが切断したことに気づかなかった
            return cdb_no;
        }
        else{
            //ただの重複リクエスト
            return -1;
        }
    }
    else{
        //重複リクエストではない
        return -2;
    }
}

//MACアドレスからCDB番号を求める
int getCdb_No(char *mac_addr)
{
    int cdb_no;

    for(cdb_no = 0 ;cdb_no < g_max_client;cdb_no++){
        if(!strcmp(Cdb_No.mac_addr,mac_addr))
            return cdb_no;
    }
    
    return -1;
}

//MACアドレスから監視デバイスかどうかを判定
//監視デバイスなら監視デバイス番号を探索失敗なら-1を返す
int getMonDevNo(char *mac_addr)
{
    int mon_dev_no;

    for(mon_dev_no = 0;mon_dev_no < g_num_mon_devs;mon_dev_no++){
        if(!strcmp(mac_addr,g_mon_devs[mon_dev_no].mac_addr))
            return mon_dev_no;
    }

    return -1;
}

//1-5の範囲の乱数を生成する
//ただし1を40%,2を30%,3を15%,4を10%,5を5%の確率で返すようにする
int getRandCP(void)
{
    int r;    

    r = rand() % 101;

    if(r < 40){
        return 1;
    }
    else if(r >= 40 && r < 70){
        return 2;
    }
    else if(r >= 70 && r < 85){
        return 3;
    }
    else if(r >= 85 && r < 95){
        return 4;
    }
    else{
        return 5;
    }
}

//cctに登録する関数
void registerToCct(char *mac_addr,int cp)
{
    int i;

    for(i = 0;i < sg_num_cct_ent;i++){
        if(sg_cct[i].st == EMPTY){
            strcpy(sg_cct[i].mac_addr,mac_addr);
            gettimeofday(&sg_cct[i].cut_time,NULL);
            sg_cct[i].ref_time = sg_BRT + sg_PRT[cp-1];
            sg_cct[i].st = USED;

            break;
        }
    }

}

//Accept
void recAccept(int cp,int rssi,int mon_dev_no,char *mac_addr)
{
    int cdb_no;

    //CDBの空いている場所を探索する
    //ここはロックしなくてもいい
	//デバイスの数が増えるのは、Acceptの時だけだから
    for(cdb_no = 0;cdb_no < g_max_client;cdb_no++){
        if(Cdb_No.st == CLOSED2){
            break;
        }
    }

    //このデバイスが監視デバイスか調べる
    if(mon_dev_no != -1){
        Cdb_No.dev_no = mon_dev_no;
        g_mon_devs[Cdb_No.dev_no].cdb_no = cdb_no;
        dprint(MSG_PRIO_HIGH,"ACCEPT: %s(cp = %d rssi = %d)\n", \
                g_mon_devs[Cdb_No.dev_no].dev_name,cp,rssi\
              );
    }
    else{
        dprint(MSG_PRIO_HIGH,"ACCEPT: %s(cp = %d rssi = %d)\n", \
                mac_addr,cp,rssi\
              );
    }

    //接続時間の取得
    gettimeofday(&Cdb_No.connected_time,NULL);

    Cdb_No.cp = cp;
    Cdb_No.rssi = rssi;

    strcpy(Cdb_No.mac_addr,mac_addr);

    pthread_mutex_lock(&g_cdb_mg.num_con_mtx);
    g_cdb_mg.num_cons++;
    pthread_mutex_unlock(&g_cdb_mg.num_con_mtx);

	g_cdb_mg.nc_cp[cp-1]++;

    Cdb_No.st = CONNECTED;
}

//Swap
//1,2,3,6は新規リクエストの値
//cdb_noは入れ替えられるデバイスのCDB番号,mon_dev_noは新規リクエストの監視デバイス番号
void recSwap(int cp,int rssi,int cdb_no,int mon_dev_no,char *mac_addr)
{
    dprint(MSG_PRIO_HIGH,"SWAP: %s(cp = %d rssi = %d --> %s(up = %d rssi = %d)\n",\
                    PRINTNAME,Cdb_No.cp,Cdb_No.rssi,\
                    (mon_dev_no != -1)? g_mon_devs[mon_dev_no].dev_name : mac_addr,\
                    cp,rssi\
          );

    //切断処理
    conCut(cdb_no,"swap",1);
    
    //このデバイスが監視デバイスか調べる
    if(mon_dev_no != -1){
        Cdb_No.dev_no = mon_dev_no;
        g_mon_devs[mon_dev_no].cdb_no = cdb_no;
    }

    //接続時間の取得
    gettimeofday(&Cdb_No.connected_time,NULL);

    Cdb_No.cp = cp;
    Cdb_No.rssi = rssi;

    strcpy(Cdb_No.mac_addr,mac_addr);

    pthread_mutex_lock(&g_cdb_mg.num_con_mtx);
    g_cdb_mg.num_cons++;
    pthread_mutex_unlock(&g_cdb_mg.num_con_mtx);

	g_cdb_mg.nc_cp[cp-1]++;
    Cdb_No.st = CONNECTED;
}

//Reject general
void recReject(int cp,int rssi,int cdb_no,int mon_dev_no,char *mac_addr)
{
    char command[64];

	if(cdb_no != -1){
			dprint(MSG_PRIO_HIGH,"REJECT: %s(cp = %d rssi = %d) "
                    "swap point %s(cp = %d rssi = %d)\n",\
                    (mon_dev_no != -1)? g_mon_devs[mon_dev_no].dev_name : mac_addr,\
                    cp,rssi,\
                    PRINTNAME,\
                    Cdb_No.cp,Cdb_No.rssi\
          );
	}else{
			dprint(MSG_PRIO_HIGH,"REJECT: %s(cp = %d rssi = %d) \n",
                    (mon_dev_no != -1)? g_mon_devs[mon_dev_no].dev_name : mac_addr,cp,rssi);
	}

    write(sg_wfifo_fd,(void *)mac_addr,17);
}

//AcceptかSwapかRejectを決定する
int requestCheck(int cp,int rssi)
{
    int rssi_level;

    if(rssi > g_rssi_lv_high){
        rssi_level = GOOD;
    }
    else if(rssi > g_rssi_lv_middle){
        rssi_level = MIDDLE;
    }
    else{
       rssi_level = BAD;
    }

    if(g_phase_status == DETECT){
        if(sg_concm_rule[0][rssi_level][cp-1] == 1){
            return ACCEPT;
        }
        else{
            return REJECT;
        }
    }
    else{
        if(sg_concm_rule[1][rssi_level][cp-1] == 1){
            return SWAP;
        }
        else{
            return REJECT;
        }
    }
}

//minimum cpを探索する関数
//そのデバイスへのindexを返す
//CDBに要素がなかったら-1を返す
int searchMinCP(void)
{
    int cdb_no;
    int min_point,min_cp;

    if(g_cdb_mg.num_cons == 0)
        return -1;

    min_point = -1;
    min_cp = 0;
    
    //will_cut_flagが立った時点でその要素はコンテキストが異なるスレッドが使用する
    for(cdb_no = 0;cdb_no < g_max_client;cdb_no++){
        if(Cdb_No.st == CONNECTED && Cdb_No.will_cut_flag == 0){
            if(min_cp == 0 || min_cp > Cdb_No.cp){
                min_cp = Cdb_No.cp;
                min_point = cdb_no;
            }
        }
    }

    return min_point;
}

//ルールベースを構築する
void setConCMRuleBase(char *config_filename)
{
    char filename[64],st[64],*p;
    int i,j,k;
    FILE *fp;

    //configファイルの読込み
    //Monitoring Deviceを設定
    snprintf(filename,sizeof(filename),"%s/%s",g_workspace_name,config_filename);

    fp = fopen(filename,"r");
    if(fp == NULL){
        perrExit("fopen",__FILE__,__LINE__);
    }

    while(noLineFgets(st,sizeof(st),fp) != NULL){
        if(!strcmp(st,"{Connection Ctrl}"))
            break;
    }
    if(feof(fp) || strcmp(st,"{Connection Ctrl}")){
        errExit("You have to write {Connection Ctrl} field",__FILE__,__LINE__);
    }
    
    for(i = 0;i < 2;i++){
        for(j = 0;j < 3;j++){
            noLineFgets(st,sizeof(st),fp);
            p = st;
            for(k = 0;k < 5;k++){
                sg_concm_rule[i][j][k] = *p - '0';
                p += 2;
            }
        }
        //改行読み飛ばし
        noLineFgets(st,sizeof(st),fp);
    }

    fclose(fp);
}

