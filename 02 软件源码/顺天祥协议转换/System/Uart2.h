#ifndef UART2_H
#define UART2_H
#include "Public_cfg.h"
void Uart2_Init(void);              /* 初始化上位机串口。 */
uint8 Uart2_ReadByte(uint8 *value); /* 非阻塞读取一个接收字节。 */
void Uart2_ClearRx(void);           /* 原子清除接收内容和溢出标记。 */
uint8 Uart2_Overflow(void);         /* 查询接收溢出标记。 */
uint8 Uart2_TryWrite(uint8 value);  /* 空闲时发送一个字节。 */
uint8 Uart2_TxIdle(void);           /* 查询前一字节是否已发送完成。 */
#ifdef BRIDGE_HOST_TEST
void Uart2_HostReceive(uint8 value); /* 主机测试注入一个接收字节。 */
#endif
#endif
