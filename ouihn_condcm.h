/************************************************
 * ouihn connection condition control module header
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 * Description
 *  接続状態制御モジュール
 ************************************************/

#ifndef __OUIHN_CONDCM_H__
#define __OUIHN_CONDCM_H__

#include "main.h"

//ルールベースの添字にリンクさせる
enum {GOOD,MIDDLE,BAD};

extern int g_rssi_lv_middle;
extern int g_rssi_lv_high;

void *conditionCtrl(void *arg);
int getRSSI(char *mac_addr);     //接続制御するときにsnrが必要になる


#endif
