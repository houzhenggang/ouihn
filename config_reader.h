/************************************************
 * ouihn config reader header
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 * Description
 *  configから設定値を読み出し配列として保持する
 ************************************************/

#ifndef __CONFIG_READER_H__
#define __CONFIG_READER_H__

#include "ouihn_common.h"
#include "main.h"

//configは連想配列の形式で保持する
struct confElement{
    char key[16];
    char value[16];
};

void configEnd(void);                   //動的に確保した変数を解放する
char *getConfig(const char *key);       //configの値を取得する関数
void loadConfig(const char *filename);  //configの読み出し

#endif
