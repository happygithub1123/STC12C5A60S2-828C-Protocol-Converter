#ifndef SYSTEM_H
#define SYSTEM_H
#include "Public_cfg.h"
void Sys_Init(void);     /* 初始化双串口和通信计时。 */
uint16 Sys_Millis(void); /* 原子读取毫秒计数，允许自然回绕。 */
#endif
