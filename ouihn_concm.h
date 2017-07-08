/************************************************
 * ouihn connection control module header
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 * Description
 *  接続制御モジュール
 ************************************************/

#ifndef __OUIHN_CONCM_H__
#define __OUIHN_CONCM_H__

#include "main.h"
#include <sys/time.h>

#define FIFOPATH "/tmp/inohfifo"        //FIFOの場所        
#define FIFOPATH2 "/tmp/inohfifo2"        //FIFOの場所        

typedef enum {EMPTY,USED} enSTATE;

//切断されたクライアント
struct cutClient{
    enSTATE st;                         //このエントリが使用されているか
    char mac_addr[MAC_ADDR_LEN];        //MACアドレス
    struct timeval cut_time;            //cutされた時間
    int ref_time;                       //リフレッシュ時間
}; 

void *connectionCtrl(void *arg); 
int conCut(int cdb_no,char *reason,int reg_flag);             //接続を切断する関数

#endif
