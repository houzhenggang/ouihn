/************************************************
 * main header file
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 * Description
 *
 ************************************************/

#ifndef __MAIN_H__
#define __MAIN_H__

#include "config_reader.h"
#include "ouihn_ccm.h"
#include "ouihn_concm.h"
#include "ouihn_condcm.h"
#include "ouihn_common.h"


#include <pthread.h>
#include <time.h>
#include <sys/time.h>

//使用するコマンド
#define STATION_DUMP "sudo iw dev wlan1 station dump"
#define STATION_GET "sudo iw dev wlan1 station get %s 2> /dev/null"
#define STATION_DEL "sudo iw dev wlan1 station del %s 2> /dev/null"
#define SURVEY_DUMP "sudo iw dev wlan1 survey dump"
#define BEEP "aplay /home/jiro/beep.wav 2> /dev/null"

#define Cdb_No g_cdb_mg.cdb[cdb_no]     //CDBの要素を扱うときに使用するマクロ

//デバイス名を出力するときに使用する
//監視デバイスならデバイス名を出力し、そうでないならMACアドレスを出力する
#define PRINTNAME (Cdb_No.dev_no != -1)? g_mon_devs[Cdb_No.dev_no].dev_name : Cdb_No.mac_addr

//CDBの各要素の状態
//CLOSED1は終了処理に入った状態、CLOSED2は完全に終了した状態
typedef enum{ CLOSED1,CLOSED2,CONNECTED } conSTATE;

//CDBにおける各デバイスごとの要素
struct conDev{
    conSTATE st;                    //この要素の状態
    pthread_mutex_t con_dev_mtx;    //この要素のミューテックス
    char mac_addr[MAC_ADDR_LEN];    //MACアドレス
    int cp;                         //Client Priority
    int rssi;                        //RSSI
    int cond_cnt;                   //condition counter
    long int ctl;                   //Client Traffic Load
    long int tx_base_ctl;            //TX_CTLの初期値
    long int rx_base_ctl;            //RX_CTLの初期値
    long int now_tx_bytes;          //現在のtx bytes
    long int prev_tx_bytes;         //前回のtx_bytes
    long int now_rx_bytes;          //現在のrx_bytes
    long int prev_rx_bytes;         //前回のrx_bytes
    struct timeval prev_measure_time;   //前回の測定時間
    double throughput;              //スループット
    struct timeval connected_time;  //このデバイスがAPに接続した時間
	int will_cut_flag;                //切断による終了なら、このフラグを立てる
    int dev_no;                     //g_mon_devsへのインデックス.この変数でどのデバイスか特定する
};

//CDB Manager
struct cdbMG{
    pthread_mutex_t num_con_mtx;    //num_consのミューテックス
	pthread_mutex_t cut_permit_mtx;   //切断処理の同期をするためのミューテックス
    int num_cons;                   //CDBに何台のクライアントが接続しているかを保持する
    int nc_cp[5];                   //CPごとの接続台数
    struct conDev *cdb;             //Connection DataBase
};


//デバイス名の長さ
//\0を含む
#define MAX_NODE_NAME_SIZE 16

//monDevのcounterフィールドの構成要素
enum {REQUEST,CUT,ACCEPT,REJECT,SWAP};

//監視(つまり登録された)するデバイスの変数
struct monDev{
    char dev_name[MAX_NODE_NAME_SIZE];  //デバイス名
    char mac_addr[MAC_ADDR_LEN];        //MACアドレス
    int cp;                             //Client Priority
	double rssi_ave;                    //RSSIの平均
	int rssi_cnt;					    //RSSIを取得した回数
    FILE *thr_fp;                       //スループットを出力するファイル
    FILE *ctime_fp;                     //接続していた時間を出力するファイル
    time_t total_time;                  //このデバイスがAPを利用した時間
    int counter[5];                     //状態を保持する変数。上の列挙型とリンクしてる
    int cdb_no;                         //CDBの添字
};

extern struct monDev *g_mon_devs;	//監視するデバイスの配列
extern struct cdbMG g_cdb_mg;		//CDB Manager
extern int g_num_mon_devs;      	//監視するデバイスの数
extern int g_max_client;            //CDBの最大エントリ数
extern char g_workspace_name[64];	//ワークスペースのパス
extern struct timeval g_start_time;	//このプログラムを起動した時間


#endif
