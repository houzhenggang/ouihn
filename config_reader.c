/************************************************
 * ouihn config reader program
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 * Description
 *  configから設定値を読み出し配列として保持する
 ************************************************/

#include "config_reader.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//configの数.
//configファイルから読み出す数を増やしたい時は、ここも増やすこと
#define MAXNUMCONF 23

//Initial settings(default setting)
//If you want to expand this program, then you have to modify this array.
//設定値
//初期化で代入されている値は、デフォルト値
//プログラムを実行する時に渡すconfigfileの値を読み込んで値を変更する
struct confElement sg_CONF[MAXNUMCONF]={
                                        {"NO_REG_DEV","1"},         //MACアドレスを登録していないデバイスの扱い
                                        {"NO_REG_DEV_CP","1"},     //登録していないデバイスのUP
                                        {"MAX_CLIENT","30"},        //最大接続数
                                        {"NUM_MON_DEVS","5"},       //監視デバイスの数
                                        {"REC1_INVAL","1"},         //監視デバイスの情報を出力するインターバル
                                        {"REC2_INVAL","10"},        //監視デバイスの情報を出力するインターバル
                                        {"COG_DT_INVAL","10"},      //輻輳検知インターバル
                                        {"COG_MN_INVAL","5"},       //輻輳監視インターバル
                                        {"COG_AV_INVAL","5"},       //輻輳回避インターバル
                                        {"COG_THRESH","1000000"},   //輻輳閾値
                                        {"MN_SEC","20"},            //輻輳監視を行う時間
                                        {"MIN_NUM_COM_DEV","3"},    //最小接続台数
                                        {"THR_GET_INVAL","1"},      //スループットを取得するインターバル
										{"CH_UTIL_THRESH","80"},	//チャネル使用率の閾値
                                        {"SURVEY_INVAL","1"},       //チャネル使用率とノイズ値を取得するインターバル
                                        {"COND_CHK_INVAL","5"},     //通信状態チェックのインターバル
                                        {"COND_CNT_MAX","3"},       //通信状態カウンタの最大値
                                        {"NUM_CCT_ENT","20"},       //cut client tableのエントリ数
                                        {"BRT","10"},               //Basic Refresh Time
                                        {"DEBUG_MODE","1"},         //デバックモード
                                        {"CCDISPLAY_INVAL","30"},   //CPごとの接続台数などを表示するインターバル
                                        {"RSSI_LEVEL_MID","-80"},   //RSSIレベルの閾値
                                        {"RSSI_LEVEL_HIGH","-65"}   //RSSIレベルの閾値
									};


static void setConfig(const char *);

//キーから値を取得する関数
char *getConfig(const char *key)
{
    int i;

    for(i = 0;i < MAXNUMCONF;i++){
        if(!strcmp(key,sg_CONF[i].key)){
            return sg_CONF[i].value;
        }
    }
    dprint(MSG_PRIO_HIGH,"Can not search %s\n",key);

    return "0";
}
	
//configファイルを読み込み変数に取り込む
void loadConfig(const char *filename)
{
    FILE *rfp;
    char st[64],conf[32];
    int i;
    char *p,*pcon;
    
    if((rfp = fopen(filename,"r")) == NULL){
       errExit("config file open error",__FILE__,__LINE__);
    }

    //各configを読み込む
    while(noLineFgets(st,sizeof(st),rfp) != NULL){
       
        //delete white spaces
        for(p = st;isspace(*p);p++)
            ;

        if(*p == '#' || *p == '\0'){
            //This line is comment or blank
            continue;
        }       

        if(!strcmp(st,"[Monitoring Device]")){
            break;
        }

        for(pcon = conf;*p != '\0' && *p != '#';p++){
            if(isspace(*p))
                continue;
            else{
                *pcon=*p;
                pcon++;
            }
        }
        *pcon='\0';

        setConfig(conf);
    }

    if(feof(rfp) || strcmp(st,"[Monitoring Device]")){
        errExit("You have to write [Monitoring Device] field",__FILE__,__LINE__);
    }

    fclose(rfp);
}

//連想配列にキーと値をセットする
void setConfig(const char *conf)
{
    char tkey[16],tvalue[16];
    int i;


    for(i = 0;*conf != '=';i++,conf++){
        tkey[i] = *conf;
    }
    tkey[i]='\0';

    for(i = 0,++conf;*conf != '\0';i++,conf++){
        tvalue[i] = *conf;
    }
    tvalue[i] = '\0';

    //search key and set value
    for(i = 0;i < MAXNUMCONF;i++){
        if(!strcmp(tkey,sg_CONF[i].key)){
			strcpy(sg_CONF[i].value,tvalue);
            break;
        }
    }
}
