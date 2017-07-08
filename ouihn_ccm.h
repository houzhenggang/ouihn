/************************************************
 * inoh_net program
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2016/11/30
 *
 * Description
 *  iNOHにおける輻輳制御関連の関数を宣言する
 * 
 ************************************************/

#ifndef __OUIHN_CONG_H__
#define __OUIHN_CONG_H__

#include "main.h"

enum {DETECT,MONITOR,AVOID};    //輻輳フェーズの状態
extern int g_phase_status;     //輻輳が発生している間立てる

void *congestionCtrl(void *arg);

#endif
