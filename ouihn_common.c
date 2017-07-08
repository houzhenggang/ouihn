/************************************************
 * ouihn common program
 *
 * Author:Takaaki Inaba(g.takaaki.inaba@gmail.com)
 * Create date:2017/2/8
 *
 ************************************************/

#include "ouihn_common.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>

#include <sys/time.h>

//デフォルトではすべてのメッセージを出力する
int g_debug_mode = 1;

static FILE *log_fp = NULL;

//可変個引数に対応したログ出力関数
//priorityは、メッセージをディスプレイに出力するかを識別する
//メッセージがMSG_PRIO_LOWでg_debug_modeが0ならディスプレイに出力しない
//すべてのメッセージをログファイルに出力する
void dprint(int priority,const char *msg, ...)
{
    va_list args;
    char buf[4096];
    char tbuf[64];

    va_start(args,msg);

    vsnprintf(buf,sizeof(buf),msg,args);
    if(g_debug_mode == 1 || priority == MSG_PRIO_HIGH){
        printf("%s <%ld> %s",printTime(tbuf,sizeof(tbuf)),(long)pthread_self(),buf);
        fflush(stdout);
    }
    
    fprintf(log_fp,"%s <%ld> %s",printTime(tbuf,sizeof(tbuf)),(long)pthread_self(),buf);
   
    va_end(args);

    fflush(log_fp);
}

//dprintの出力の時間を時刻へ変更した関数
void dprint2(FILE *fp,const char *msg, ...)
{
    va_list args;
    char buf[4096];
    char tbuf[64];

    va_start(args,msg);

    vsnprintf(buf,sizeof(buf),msg,args);

    fprintf(fp,"%s %s",printTime2(tbuf,sizeof(tbuf)),buf);
    fflush(fp);
   
    va_end(args);
}

//stderrにmsgで指定されたメッセージを出力し終了する
//filenameとlineはこの関数を呼び出したプログラム名と行数
void errExit(const char *msg,const char *filename,int line)
{
    char tbuf[64];

    fprintf(stderr,"%s %s  %s:%d\n",printTime(tbuf,sizeof(tbuf)),msg,filename,line);
    exit(EXIT_FAILURE);
}


//改行を含まないfgets.行読込みで改行まで読み込めなかったら、改行が出るまで破棄する
//標準入力を渡されたら、普通のfgetsを呼び出す
char *noLineFgets(char *s,size_t size,FILE *fp)
{
    int i,c,moreFlag = 1;

    if(fp == stdin){
        return fgets(s,size,fp);
    }

    for(i = 0;i < (size - 1);i++){
        c = fgetc(fp);
        if(c == EOF){
            s[i] = '\0';
            return NULL;
        }
        if(c == '\n'){
            moreFlag = 0;
            break;
        }
        s[i] = (char)c;
    }
    
    s[i] = '\0';
    if(moreFlag){
        while((c = fgetc(fp)) != '\n')
            ;
    }
    return s;
}

//エラー処理付きのmalloc関数
void *Malloc(const size_t size)
{
    void *p;

    if((p = malloc(size)) == NULL){
        errExit("Malloc",__FILE__,__LINE__);
    }

    return p;
}

//デバック関連処理の初期化
void logOpen(const char *dirname)
{
    char filename[64];

    snprintf(filename,sizeof(filename),"%s/log.ouihn",dirname);
    if((log_fp = fopen(filename,"w")) == NULL){
        perrExit("log file open error",__FILE__,__LINE__);
    }
}

//リソース解放
void logClose(void)
{
    if(g_debug_mode == 0){
        return;
    }
    fclose(log_fp);
}

//エラー出力はするが、プログラムの終了はしない
void perr(const char *msg,const char *filename,int line)
{
    char buf[128],tbuf[64];

    strerror_r(errno,buf,sizeof(buf));
    dprint(MSG_PRIO_HIGH,"%s: %s  %s:%d\n",msg,buf,filename,line);
}

//errnoを参照するerrExit
void perrExit(const char *msg,const char *filename,int line)
{
    char buf[128],tbuf[64];

    strerror_r(errno,buf,sizeof(buf));
    fprintf(stderr,"%s %s: %s  %s:%d\n",printTime(tbuf,sizeof(tbuf)),msg,buf,filename,line);
    exit(EXIT_FAILURE);
}	

//bufに現在の時刻の文字列を書き込む
//sizeには、bufの配列のサイズを指定する
//戻り値として、bufへのポインタを返す
char *printTime(char *buf,size_t size)
{
    struct timeval tv;
    struct tm tm1;

    gettimeofday(&tv,NULL);
    localtime_r(&tv.tv_sec,&tm1);

    snprintf(buf,size,"%02d:%02d:%02d:%02d:%02d.%03ld : ",\
            tm1.tm_mon+1,tm1.tm_mday,tm1.tm_hour,tm1.tm_min,tm1.tm_sec,(long)(tv.tv_usec * 0.001));

    return buf;
}

char *printTime2(char *buf,size_t size)
{
    struct timeval tv;
    struct tm tm1;

    gettimeofday(&tv,NULL);
    localtime_r(&tv.tv_sec,&tm1);

    snprintf(buf,size,"%02d:%02d:%02d.%03ld : ",\
            tm1.tm_hour,tm1.tm_min,tm1.tm_sec,(long)(tv.tv_usec * 0.001));

    return buf;
}

