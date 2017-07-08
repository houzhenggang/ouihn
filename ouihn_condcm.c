/************************************************
 * ouihn connection condition control module
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 * Description
 *  接続状態制御モジュール
 ************************************************/

#include "ouihn_condcm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {CONCUT,COUNTUP,STAY};

//RSSIのレベル閾値
int g_rssi_lv_middle;   
int g_rssi_lv_high;


//通信状態制御用ルールベース
static int sg_condcm_rule[2][3][5];

static int ruleCheck(int cp,int rssi);
static void setCondCMRuleBase(char *config_filename);

//通信状態制御を行うスレッド
//station getでCDBに接続されているデバイスだけ取得した方が効率がいいかも
void *conditionCtrl(void *arg)
{
    int inval;
    int cdb_no,rlt,rssi,max_counter;

    pthread_detach(pthread_self());

    dprint(MSG_PRIO_LOW,"<<<<<  Start Condition Control Thread  >>>>>\n");

    inval = atoi(getConfig("COND_CHK_INVAL"));
    max_counter = atoi(getConfig("COND_CNT_MAX"));
    g_rssi_lv_middle = atoi(getConfig("RSSI_LEVEL_MID"));
    g_rssi_lv_high = atoi(getConfig("RSSI_LEVEL_HIGH"));

    setCondCMRuleBase((char *)arg);

    for(;;){
        //ノイズ値はccmのsurveyThreadで取得されている
        for(cdb_no = 0;cdb_no < g_max_client;cdb_no++){
            pthread_mutex_lock(&Cdb_No.con_dev_mtx);

            if(Cdb_No.st == CONNECTED){
                if((rssi = getRSSI(Cdb_No.mac_addr)) != 0){
                    if(Cdb_No.dev_no != -1){
                        g_mon_devs[Cdb_No.dev_no].rssi_ave += rssi;
                        g_mon_devs[Cdb_No.dev_no].rssi_cnt++;
                    }
                    Cdb_No.rssi = rssi;

                    //カウントアップするか判定する
                    if( (rlt = ruleCheck(Cdb_No.cp,Cdb_No.rssi)) == STAY){
                        //通信状態がよければカウンタを0リセットする
                        Cdb_No.cond_cnt = 0;
                    }
                    else if(rlt == COUNTUP){
                        Cdb_No.cond_cnt++;
                    }
                    else{
                        Cdb_No.cond_cnt += max_counter;
                    }
                }
                else{ //RSSIの取得に失敗した場合
                    //カウンタを2カウントアップする
                    Cdb_No.cond_cnt += 2;
                    pthread_mutex_unlock(&Cdb_No.con_dev_mtx);
                    continue;
                }
                //ここで切断処理に入るのはデッドロックになる
                
            }

            pthread_mutex_unlock(&Cdb_No.con_dev_mtx);
        }

        //カウンタが設定値を超えているクライアントの接続を切断する
        //will_cut_flagをたてて、切断ミューテックスを取得しconCutを呼ぶ
        for(cdb_no = 0;cdb_no < g_max_client;cdb_no++){
            //デバッグモードならなんか出力する
            if(Cdb_No.cond_cnt > max_counter){
                pthread_mutex_lock(&g_cdb_mg.cut_permit_mtx);
                Cdb_No.will_cut_flag = 1;
                pthread_mutex_unlock(&g_cdb_mg.cut_permit_mtx);

                dprint(MSG_PRIO_LOW,"%s,%d\n",PRINTNAME,Cdb_No.rssi);
                
                conCut(cdb_no,"Bad Condition",0);
            } 
        }

        sleep(inval);
    }

    dprint(MSG_PRIO_LOW,"<<<<<  End Condition Control Thread  >>>>>\n");

    return (void *)0;
}

//mac_addrに指定されたデバイスのRSSIを取得する
//エラーの場合は 0を返す
//popenの呼び出しが多すぎて負荷が高いかも.その場合は、station dumpで一気に呼び出すようにする
int getRSSI(char *mac_addr)
{
    char command[64],buf[64],trssi[5],*p;
    int i,rssi = 0;
    FILE *fp;

    //station getでそのデバイスのRSSIを取得
    snprintf(command,sizeof(command),STATION_GET,mac_addr);
    if((fp = popen(command,"r")) == NULL){
        perr("station get failure",__FILE__,__LINE__);
        goto RETURN;
    }

    //read result and edit anything
    while(fgets(buf,sizeof(buf),fp) != NULL){
        if(strstr(buf,"signal:") != NULL){
            for(i = 0,p = buf;(*p != '-' && (i < sizeof(buf)));p++,i++)
                ;
            if(i == sizeof(buf))
                goto RETURN;

            trssi[0] = *p;
            p++;
            //RSSI読み込み
            for(i = 1;i < 4 && isdigit(*p);i++,p++){
                trssi[i] = *p;
            }
            trssi[i] = '\0';
            rssi = atoi(trssi);

            break;
        }
    }

RETURN:
    pclose(fp);

    return rssi; 
}

//ルールベースを参照して処理を判定する
int ruleCheck(int cp,int rssi)
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
        if(sg_condcm_rule[0][rssi_level][cp-1] == 2){
            return STAY;
        }
        else if(sg_condcm_rule[0][rssi_level][cp-1] == 1){
            return COUNTUP;
        }
        else{
            return CONCUT;
        }
    }
    else{
        if(sg_condcm_rule[1][rssi_level][cp-1] == 2){
            return STAY;
        }
        else if(sg_condcm_rule[1][rssi_level][cp-1] == 1){
            return COUNTUP;
        } 
        else{
            return CONCUT;
        }
    }

}

//ルールベースを構築する
void setCondCMRuleBase(char *config_filename)
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
        if(!strcmp(st,"{Condition Ctrl}"))
            break;
    }
    if(feof(fp) || strcmp(st,"{Condition Ctrl}")){
        errExit("You have to write {Condition Ctrl} field",__FILE__,__LINE__);
    }
    
    for(i = 0;i < 2;i++){
        for(j = 0;j < 3;j++){
            noLineFgets(st,sizeof(st),fp);
            p = st;
            for(k = 0;k < 5;k++){
                sg_condcm_rule[i][j][k] = *p - '0';
                p += 2;
            }
        }
        //改行読み飛ばし
        noLineFgets(st,sizeof(st),fp);
    }

    fclose(fp);
}

