#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <pthread.h>

void ESLOG_ERR(const char *szFormat, ...)
{
#ifdef DEBUG_LOG
    char pDate[512] = {0};
#ifdef TIME_LOG
    struct timeval start;
    gettimeofday(&start, NULL);
    sprintf(pDate, "time->%ld:%ld [%d-%ld] %s:(%d) ERR:", start.tv_sec, start.tv_usec, getpid(), pthread_self(), __FILE__, __LINE__);
#else //NOT TIME_LOG
    //sprintf(pDate, "[%d-%ld] %s(%d) ERR:", getpid(), pthread_self(), __FILE__, __LINE__);
    sprintf(pDate, "[%d] %s(%d) ERR:", getpid(), pthread_self(), __FILE__);
#endif
    char *szBufTemp = pDate + strlen(pDate);

    va_list valist;
    va_start(valist, szFormat);
    vsnprintf(szBufTemp, 512 - strlen(pDate), szFormat, valist);
    va_end(valist);

    printf("\033[31m%s\033[0m", pDate);
#endif /* DEBUG_LOG */
}

void ESLOG_INFO(const char *szFormat, ...)
{
#ifdef DEBUG_LOG
    char pDate[512] = {0};
#ifdef TIME_LOG
    struct timeval start;
    gettimeofday(&start,NULL);
    sprintf(pDate, "time->%ld:%ld [%d-%ld] INFO:", start.tv_sec, start.tv_usec, getpid(), pthread_self());
#else //NOT TIME_LOG
    //sprintf(pDate, "[%d-%ld] INFO:", getpid(), pthread_self());
#endif

    char *szBufTemp = pDate + strlen(pDate);

    va_list valist;
    va_start(valist, szFormat);
    vsnprintf(szBufTemp, 512 - strlen(pDate), szFormat, valist);
    va_end(valist);

    printf("%s", pDate);
#endif /* DEBUG_LOG */
}

void ESLOG_BIN(const char * name,char * data,unsigned int len)
{
#ifdef DEBUG_LOG
    int i = 0;
    char *tmpData = data;
    printf("%s",name);
    for (i = 0; i<len; i++) {
        if(0 == i%16)
            printf("\n%02x ",data[i]);
        else
            printf("\033[%dC%02x ",3*(i%16),data[i]);

        printf("\033[%dC%c\033[u",60-(1*(i%16)),data[i]);
    }

    printf("\n");
#endif
}
