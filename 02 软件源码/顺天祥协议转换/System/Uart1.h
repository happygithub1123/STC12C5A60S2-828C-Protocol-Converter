#ifndef UART1_H
#define UART1_H
#include "project_types.h"

void Uart1_Init(void);
uint16 Uart1_Count(void);
/* Peek only inside a Count() snapshot; unread bytes never get overwritten. */
uint8 Uart1_Peek(uint16 offset);
void Uart1_Drop(uint16 length);
void Uart1_ClearRx(void);
uint8 Uart1_Overflow(void);
uint8 Uart1_TryWrite(uint8 value);
uint8 Uart1_TxIdle(void);
#ifdef BRIDGE_HOST_TEST
void Uart1_HostReceive(uint8 value);
#endif
#endif
