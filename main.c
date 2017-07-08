/************************************************
 * ouihn main
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 * Description
 *  
 ************************************************/

/*******************************************************************************************************
 * 動作軽量化のための改良ポイント
 *　・popen関数の呼び出し回数を減らす。station getをstation dumpに変えて文字列処理を一気にかける
 *　・mutexのロックを減らす。配列をトレースするときは、ノンブロックロックなどを考える
 *　・データ構造を変更する。ただし、CDBを単純なリスト構造にしても空間局所性が失われるため逆効果になる
 *    可能性がある。最大要素数（データ数）が200程度であることを意識するように
 *  ・起動するスレッド数を減らす、RasPiのランレベルを3にするなど。RasPiのCPUのコア数と考慮した設計にする
********************************************************************************************************/

#include "main.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>

struct monDev *g_mon_devs;				//監視するデバイスの配列
struct cdbMG g_cdb_mg;					//CDB Maneger
int g_num_mon_devs;      				//監視するデバイスの数。configで読み出した値がセットされる
int g_max_client;                       //CDBのエントリ数
char g_workspace_name[64];				//ワークスペースのパス
struct timeval g_start_time;		    //このプログラムを起動した時間

//関数プロトタイプ宣言
static void *ccdispThread(void *arg);
static void cpConfigFile(const char *config_filename);
static int mkWorkspace(const char *dirname);
static void ouihnInit(const char *config_filename);
static void *recThread(void *arg);
static void rec2Thread(union sigval sv);
static void timerInit(void);


int main(int argc,char *argv[])
{
    pthread_t cong_th_id,rec_th_id,conctrl_th_id,condctrl_th_id,ccdisp_th_id;

    if(argc != 3 || !strcmp(argv[1],"-h")){
        fputs("usage: inoh config_filename workspace_name\n",stderr);
        exit(EXIT_SUCCESS);
    }

    //configを読み込む
    loadConfig(argv[1]);

    //workspaceを作成する
    mkWorkspace(argv[2]);

    //config fileをコピーする
    cpConfigFile(argv[1]);

	//recorder2インターバルタイマの設定
    //設定されたインターバルタイマが経過したら、設定された関数が別スレッドで起動する
    timerInit();

    //実験を開始した時間を取得
    gettimeofday(&g_start_time,NULL);
 
    //Debugモードの設定と初期化
    g_debug_mode = atoi(getConfig("DEBUG_MODE"));
    logOpen(g_workspace_name);

    //オブジェクトの初期化を行う
   	ouihnInit(argv[1]);

    //接続制御を行う
    if(pthread_create(&conctrl_th_id,NULL,connectionCtrl,argv[1]) != 0){
        perrExit("pthread_create",__FILE__,__LINE__);
    }

    //通信状態制御を行う
    if(pthread_create(&condctrl_th_id,NULL,conditionCtrl,argv[1]) != 0){
        perrExit("pthread_create",__FILE__,__LINE__);
    }

    //輻輳制御スレッドを起動する
    if(pthread_create(&cong_th_id,NULL,congestionCtrl,argv[1]) != 0){
        perrExit("pthread_create",__FILE__,__LINE__);
    }

    //CPごとの接続状況、監視デバイスの接続状況をディスプレイに出力する
    if(pthread_create(&ccdisp_th_id,NULL,ccdispThread,NULL) != 0){
        perrExit("pthread_create",__FILE__,__LINE__);
    }

    //num_consと輻輳状態を記録する
    if(pthread_create(&rec_th_id,NULL,recThread,NULL) != 0){
        perrExit("pthread_create",__FILE__,__LINE__);
    }

    pthread_join(conctrl_th_id,NULL);

    logClose();

	free(g_cdb_mg.cdb);
    free(g_mon_devs);

    return 0;
}

//CPごとの接続状況、監視デバイスの接続状況をディスプレイに出力する
void *ccdispThread(void *arg)
{
    int inval,cdb_no;
    char buf[4096] = {'\0'},buf2[32],*p;

    inval = atoi(getConfig("CCDISPLAY_INVAL"));

    for(;;){
        sleep(inval);

        dprint(MSG_PRIO_HIGH,"******   num_cons:%d  cp1:%d  cp2:%d  cp3:%d  cp4:%d  cp5:%d   ******\n",\
                g_cdb_mg.num_cons,g_cdb_mg.nc_cp[0],g_cdb_mg.nc_cp[1],g_cdb_mg.nc_cp[2],g_cdb_mg.nc_cp[3],g_cdb_mg.nc_cp[4]);
        
        for(cdb_no = 0;cdb_no < g_max_client;cdb_no++){
            if(Cdb_No.dev_no != -1){
                snprintf(buf2,sizeof(buf2),"%s:%d/%d, ",g_mon_devs[Cdb_No.dev_no].dev_name,Cdb_No.cp,Cdb_No.rssi);
                strncat(buf,buf2,sizeof(buf2));
            }
        }
        p = strrchr(buf,',');
        if(p != NULL)
            *p = '\0';

        dprint(MSG_PRIO_HIGH,"Connected Monitoring Device: { %s }\n",buf);

        buf[0] = '\0';
    }

    return (void *)0;
}

//configをworkspaceにコピーする
void cpConfigFile(const char *config_filename)
{
    char filename[64],buf[1024];
    FILE *wfp,*rfp;

    rfp = fopen(config_filename,"r");
    if(rfp == NULL){
        perrExit("cpConfigFile open error",__FILE__,__LINE__);
    }

    snprintf(filename,sizeof(filename),"%s/%s",g_workspace_name,config_filename); 
    wfp = fopen(filename,"w");
    if(wfp == NULL){
        perrExit("cpConfigFile open error",__FILE__,__LINE__);
    }

    while(fgets(buf,sizeof(buf),rfp) != NULL){
        fputs(buf,wfp);
    }

    fclose(wfp);
    fclose(rfp);
}

//workspaceを作成する
//成功なら0を失敗なら-1を返し、errnoをセットする
int mkWorkspace(const char *dirname)
{
    int ret,i;

    snprintf(g_workspace_name,sizeof(g_workspace_name),"%s",dirname);
    errno = 0;
    ret = mkdir(g_workspace_name,S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
    //既にそのディレクトリが存在している場合
    if(ret == -1 && errno == EEXIST){
        for(i = 1;;i++){
            snprintf(g_workspace_name,sizeof(g_workspace_name),"%s:%d",dirname,i);
            ret = mkdir(g_workspace_name,S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
            if(ret == 0)
                return 0;
        }
    }
    
    return -1;
}


//各種値を初期化する
void ouihnInit(const char *config_filename)
{
    int i,ite = 0,cdb_no,nn,cp;
    char filename[64],tcp[4],*p;
    char buf[128],st[64],mac_addr[MAC_ADDR_LEN],dev_name[MAX_NODE_NAME_SIZE];
    FILE *fp;

    //グローバル変数の初期化
    g_max_client = atoi(getConfig("MAX_CLIENT"));
    g_num_mon_devs = atoi(getConfig("NUM_MON_DEVS"));

    //監視デバイスの配列を確保する
    g_mon_devs = (struct monDev *)Malloc(sizeof(struct monDev) * g_num_mon_devs);

    //configファイルの読込み
    //Monitoring Deviceを設定
    snprintf(filename,sizeof(filename),"%s/%s",g_workspace_name,config_filename);

    fp = fopen(filename,"r");
    if(fp == NULL){
        perrExit("fopen",__FILE__,__LINE__);
    }

    while(noLineFgets(st,sizeof(st),fp) != NULL){
        if(!strcmp(st,"[Monitoring Device]"))
            break;
    }
    
    if(feof(fp) || strcmp(st,"[Monitoring Device]")){
        errExit("You have to write [Monitoring Device] field",__FILE__,__LINE__);
    }

	//Monitoring Deviceを解釈する
    while(noLineFgets(st,sizeof(st),fp) != NULL){
        //終了条件
        if(!strcmp(st,"[Rules]")){
            break;
        }

        //delete white spaces
        for(p = st;isspace(*p);p++)
            ;
                                            
        if(*p == '#' || *p == '\0'){
            //This line is comment or blank
            continue;
        }

        //デバイス名を読み込み
        for(i = 0,p = st;i < (MAX_NODE_NAME_SIZE-1) && *p != '%';p++){
            if(isspace(*p))
                continue;
            dev_name[i] = *p;
            i++;
        }
        dev_name[i] = '\0';

        if(*p != '%'){
            for(;*p != '%';p++)
                ;
        }

        //MACアドレスを読み込み
        p++;
        for(i = 0;i < (MAC_ADDR_LEN-1) && *p != '%';p++){
            if(isspace(*p))
                continue;
            mac_addr[i] = *p;
            i++;
        }
        if(i != (MAC_ADDR_LEN-1)){
            errExit("MAC address read error",__FILE__,__LINE__);
        }
        mac_addr[i] = '\0';

        for(i = 0;i < (MAC_ADDR_LEN - 1);i++)
            mac_addr[i] = tolower(mac_addr[i]);

        if(*p != '%'){
            for(;*p != '%';p++)
                ;
        }
        
        //CPを読み込み
        p++;
        for(i = 0;i < 3 && *p != '\0';p++){
            if(!isdigit(*p))
                continue;
            tcp[i] = *p;
            i++;
        }

        tcp[i] = '\0';
        cp = atoi(tcp);
        
        //値をセットする
        strcpy(g_mon_devs[ite].mac_addr,mac_addr);        
        strcpy(g_mon_devs[ite].dev_name,dev_name);
        g_mon_devs[ite].cp = cp;
        ite++;
        
        if(ite == g_num_mon_devs)
            break;
    }
   
    if(ite != g_num_mon_devs){
        errExit("You have to write more [Monitoring Device] field",__FILE__,__LINE__);
    }

	fclose(fp);

    //CDBの初期化
    pthread_mutex_init(&g_cdb_mg.num_con_mtx,NULL);
    pthread_mutex_init(&g_cdb_mg.cut_permit_mtx,NULL);
    g_cdb_mg.num_cons = 0;
    g_cdb_mg.nc_cp[0] = g_cdb_mg.nc_cp[1] = g_cdb_mg.nc_cp[2] = g_cdb_mg.nc_cp[3] = g_cdb_mg.nc_cp[4] = 0;
    g_cdb_mg.cdb = (struct conDev *)Malloc(sizeof(struct conDev) * g_max_client);
    for(cdb_no = 0;cdb_no < g_max_client;cdb_no++){
        Cdb_No.st = CLOSED2; 
        pthread_mutex_init(&Cdb_No.con_dev_mtx,NULL);
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
    }

    //mon_devsの初期化
    //MACアドレス、デバイス名、CPはconfig_readerで読み込む
    for(i = 0;i < g_num_mon_devs;i++){
        g_mon_devs[i].rssi_ave = 0.0;
		g_mon_devs[i].rssi_cnt = 0;
        g_mon_devs[i].total_time = 0;
        g_mon_devs[i].counter[REQUEST] = 0;
        g_mon_devs[i].counter[CUT] = 0;
        g_mon_devs[i].counter[ACCEPT] = 0;
        g_mon_devs[i].counter[REJECT] = 0;
        g_mon_devs[i].counter[SWAP] = 0;
        g_mon_devs[i].cdb_no = -1;

        //各デバイスのスループットを出力するファイルを作成する
        snprintf(filename,sizeof(filename),"%s/thr_%s",\
                                    g_workspace_name,g_mon_devs[i].dev_name);
        g_mon_devs[i].thr_fp = fopen(filename,"w");
        if(g_mon_devs[i].thr_fp == NULL){
            perrExit("Throughput File open error",__FILE__,__LINE__);
        }
        setbuf(g_mon_devs[i].thr_fp,NULL);
        
        //各デバイスが接続していた時間を出力するファイルを作成する
        snprintf(filename,sizeof(filename),"%s/c_time_%s",\
                                    g_workspace_name,g_mon_devs[i].dev_name);
        g_mon_devs[i].ctime_fp = fopen(filename,"w");
        if(g_mon_devs[i].ctime_fp == NULL){
            perrExit("c_time file open error",__FILE__,__LINE__);
        }
        setbuf(g_mon_devs[i].ctime_fp,NULL);
    }

}


//実験中の各値を記録するスレッド
void *recThread(void *arg)
{
    char filename[64];
    int cnt,i,inval;
    FILE *cong_fp;
    
    pthread_detach(pthread_self());

    inval = atoi(getConfig("REC1_INVAL"));

    snprintf(filename,sizeof(filename),"%s/congstion_time",g_workspace_name);
    cong_fp = fopen(filename,"w");
    if(cong_fp == NULL){
        perrExit("congestion_time open error",__FILE__,__LINE__);
    }

    setbuf(cong_fp,NULL);

    //輻輳状態、接続台数、各端末のスループットを出力する
    for(;;){
        for(cnt = 0;;cnt++){
            dprint2(cong_fp,"%d %d\n",g_cdb_mg.num_cons,g_phase_status);
            for(i = 0;i < g_num_mon_devs;i++){
                dprint2(g_mon_devs[i].thr_fp,"%.2f\n",\
                                (g_mon_devs[i].cdb_no != -1)? \
                                g_cdb_mg.cdb[g_mon_devs[i].cdb_no].throughput : 0.0\
                        );
            }

            sleep(inval);
        }
    }

    for(i = 0;i < g_num_mon_devs;i++){
        fclose(g_mon_devs[i].thr_fp);
    }

    fclose(cong_fp);

    return (void *)0;
}

//rec2_timerが切れることで起動するスレッド
//実験の途中経過を出力する
//監視デバイスの途中結果をファイルに書き出す
void rec2Thread(union sigval sv)
{
    int i;
    char tbuf[64],filename[64];
    struct timeval now_time;
    FILE *fp1,*fp2;

    pthread_detach(pthread_self());
    
	dprint(MSG_PRIO_LOW,"<<<< Start RECORD2 Thread >>>>\n");

    snprintf(filename,sizeof(filename),"%s/%s",g_workspace_name,"prog_report");
    fp1 = fopen(filename,"w");
    if(fp1 == NULL){
        perr("prog_report open error",__FILE__,__LINE__);
        return;
    }

    snprintf(filename,sizeof(filename),"%s/%s",g_workspace_name,"suc_ocu");
    fp2 = fopen(filename,"w");
    if(fp2 == NULL){
        perr("suc_ocu fopen",__FILE__,__LINE__);
        return;
    }

    gettimeofday(&now_time,NULL);
    fprintf(fp1,"%s\n",printTime(tbuf,sizeof(tbuf)));
    for(i = 0;i < g_num_mon_devs;i++){
		if(g_mon_devs[i].counter[REQUEST] != 0){
        	fprintf(fp1,"[%s]\n",g_mon_devs[i].dev_name);
        	fprintf(fp1,"    CP = %d  Average of RSSI = %.2f\n",\
                            g_mon_devs[i].cp,\
                            (g_mon_devs[i].rssi_cnt != 0)? \
                            (g_mon_devs[i].rssi_ave / g_mon_devs[i].rssi_cnt) : \
                            g_mon_devs[i].rssi_ave\
                    );
        	fprintf(fp1,"    Number of REQUEST %d\n",g_mon_devs[i].counter[REQUEST]);
        	fprintf(fp1,"    Number of CUT %d\n",g_mon_devs[i].counter[CUT]);
        	fprintf(fp1,"    Number of ACCEPT %d\n",g_mon_devs[i].counter[ACCEPT]);
        	fprintf(fp1,"    Number of REJECT %d\n",g_mon_devs[i].counter[REJECT]);
        	fprintf(fp1,"    Number of SWAP %d\n",g_mon_devs[i].counter[SWAP]);
            fprintf(fp1,"    Total Connected Time %ld s (%d%%)\n",\
            	            (g_mon_devs[i].counter[CUT] == 0)? \
                            (now_time.tv_sec - g_start_time.tv_sec) :\
                            (long)g_mon_devs[i].total_time,\
            	            (g_mon_devs[i].counter[CUT] == 0)? \
                            100 : (int)((g_mon_devs[i].total_time * 100)/(now_time.tv_sec - g_start_time.tv_sec))\
           		    );

            
            fprintf(fp2,"%s %.2f\n",g_mon_devs[i].dev_name,\
                    (double)(g_mon_devs[i].counter[SWAP]+g_mon_devs[i].counter[ACCEPT]) * 100 / g_mon_devs[i].counter[REQUEST]
                   ); 
		}
    }

    fflush(fp1);
    fflush(fp2);

    fclose(fp1);
    fclose(fp2);

    dprint(MSG_PRIO_LOW,"<<<< End RECORD2 Thread >>>>\n");
}

//インターバルタイマの設定
//設定されたインターバルが切れるごとにスレッドを起動したいなら、ここで設定する
void timerInit(void)
{
    struct sigevent rec2_sigev;
    timer_t rec2_timer;
    struct itimerspec rec2_time;
 
    memset(&rec2_sigev,0,sizeof(rec2_sigev));

    rec2_sigev.sigev_notify = SIGEV_THREAD;

    rec2_sigev.sigev_notify_function = rec2Thread;

    rec2_sigev.sigev_notify_attributes = NULL;

    rec2_time.it_value.tv_sec = atoi(getConfig("REC2_INVAL"));
    rec2_time.it_value.tv_nsec = 0;
    rec2_time.it_interval.tv_sec = atoi(getConfig("REC2_INVAL"));
    rec2_time.it_interval.tv_nsec = 0;

    timer_create(CLOCK_REALTIME,&rec2_sigev,&rec2_timer);

    timer_settime(rec2_timer,0,&rec2_time,NULL);
}

