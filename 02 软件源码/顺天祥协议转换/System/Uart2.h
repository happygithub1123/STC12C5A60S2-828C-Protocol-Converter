#ifndef UART2_H
#define UART2_H
#include "project_types.h"
void Uart2_Init(void);
uint8 Uart2_ReadByte(uint8 *value);
void Uart2_ClearRx(void);
uint8 Uart2_Overflow(void);
uint8 Uart2_TryWrite(uint8 value);
uint8 Uart2_TxIdle(void);
#ifdef BRIDGE_HOST_TEST
void Uart2_HostReceive(uint8 value);
#endif
#endif
