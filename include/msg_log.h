#ifndef __MSG_LOG_H__
#define __MSG_LOG_H__

#ifdef __cplusplus
extern "C"{
#endif
extern void ESLOG_ERR(const char *szFormat, ...);
extern void ESLOG_INFO(const char *szFormat, ...);
extern void ESLOG_BIN(const char * name,char * data,unsigned int len);

#ifdef __cplusplus
}
#endif
#endif
