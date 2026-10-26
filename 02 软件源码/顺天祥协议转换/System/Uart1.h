#ifndef UART1_H
#define UART1_H
#include "Public_cfg.h"

void Uart1_Init(void);             /* 初始化主板串口。 */
uint16 Uart1_Count(void);          /* 获取当前可读字节数快照。 */
uint8 Uart1_Peek(uint16 offset);   /* 查看快照范围内的字节，不移动索引。 */
void Uart1_Drop(uint16 length);    /* 释放已校验且已消费的字节。 */
void Uart1_ClearRx(void);          /* 原子清除接收内容和溢出标记。 */
uint8 Uart1_Overflow(void);        /* 查询接收溢出标记。 */
uint8 Uart1_TryWrite(uint8 value); /* 空闲时发送一个字节。 */
uint8 Uart1_TxIdle(void);          /* 查询前一字节是否已发送完成。 */
#ifdef BRIDGE_HOST_TEST
void Uart1_HostReceive(uint8 value); /* 主机测试向同一环形缓冲注入字节。 */
#endif
#endif
