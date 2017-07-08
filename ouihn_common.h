
/************************************************
 * ouihn_common header file
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 * Description
 *
 *************************************************/

/************************************************************************
* プログラムのコーディングルールについて
*  構造体の宣言と自作した関数については、Low cammel caseで宣言すること。
*  基本的にOS固有のシステムコールなどはsnake caseで宣言されているので、
*  これによって、自作したものとOS固有のものを識別できる。
*  ただし、ライブラリ関数やシステムコールのラッパ関数は、その関数名の先頭を
*  大文字にして宣言すること。例えば、malloc関数のラッパ関数は、Malloc関数として
*  宣言する。
*
*  変数の宣言は、snake caseで宣言すること。
*　また、グローバル変数を宣言する際は、変数の先頭にg_をつけること。
*  ファイル内でのみ参照するグローバル変数には,先頭にsg_をつけること。
*  これによってグローバル変数、staticつきグローバル変数とローカル変数を区別する。
*  
*  ヘッダファイルのインクルードは、標準ライブラリ、システム固有のヘッダファイル
*  の順でインクルードすること。また、それぞれアルファッベット順で宣言すること。
*
*  ここに記述していること以外は、前例を倣って統一した記述を心がけること
*************************************************************************/


#ifndef __OUIHN_COMMON__
#define __OUIHN_COMMON__

#include <stdio.h>

#define MAC_ADDR_LEN 18             //MACアドレスの長さ。\0を含む

//デバッグメッセージのレベル
#define MSG_PRIO_LOW 0
#define MSG_PRIO_HIGH 1


//可変個引数に対応したログ出力関数
void dprint(int priority,const char *msg, ...);

//dprintの出力を時間から時刻へ変更した関数
void dprint2(FILE *fp,const char *msg, ...);

//stderrにmsgで指定されたメッセージを出力し終了する
//filenameとlineはこの関数を呼び出したプログラム名と行数
void errExit(const char *msg,const char *filename,int line);

//デバックモード
//debug_mode=0 デバッグメッセージをディスプレイに出力しない
//debug_mode=1 すべてのメッセージをディスプレイに出力する
extern int g_debug_mode;

//改行を含まないfgets.行読込みで改行まで読み込めなかったら、改行が出るまで破棄する
char *noLineFgets(char *s,size_t size,FILE *fp);

//エラー処理付きのmalloc関数
void *Malloc(const size_t size);

//デバック関連処理の初期化
void logOpen(const char *dirname);

//リソース解放
void logClose(void);

//errnoを参照するerrExit
void perrExit(const char *msg,const char *filename,int line);

//bufに現在の時刻の文字列を書き込む
//sizeには、bufの配列のサイズを指定する
//戻り値として、bufへのポインタを返す
char *printTime(char *buf,size_t size);

char *printTime2(char *buf,size_t size);
#endif
