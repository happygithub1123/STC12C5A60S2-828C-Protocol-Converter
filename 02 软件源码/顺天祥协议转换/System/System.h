#ifndef SYSTEM_H
#define SYSTEM_H
#include "Public_cfg.h"
void Sys_Init(void);     /* 初始化双串口及按键消抖时基，外部中断保持关闭。 */
uint16 Sys_Millis(void); /* 原子读取 16 位毫秒计数，差值按无符号回绕计算。 */
#endif
