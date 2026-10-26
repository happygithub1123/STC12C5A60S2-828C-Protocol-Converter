#include "Public_cfg.h"
#include "Uart1.h"

static volatile uint8 XDATA rx[UART1_RX_CAPACITY];
static volatile uint16 read_at, write_at;
static volatile uint8 overflow;
#ifndef BRIDGE_HOST_TEST
static volatile uint8 tx_busy;
#else
extern uint8 Host_TxReady(uint8 port);
extern void Host_Write(uint8 port, uint8 value);
#endif

void Uart1_Init(void)
{
    read_at = 0u;
    write_at = 0u;
    overflow = 0u;
#ifndef BRIDGE_HOST_TEST
    /* UART1 fixed pins: P3.0 RX input, P3.1 TX push-pull. */
    P3M1 = (P3M1 & 0xFCu) | 0x01u;
    P3M0 = (P3M0 & 0xFCu) | 0x02u;
    P3 |= 0x03u;
    SCON = 0x50u;
    tx_busy = 0u;
    ES = 1;
#endif
}

uint16 Uart1_Count(void)
{
    uint16 head, tail;
#ifndef BRIDGE_HOST_TEST
    uint8 saved;
    saved = ES;
    ES = 0;
#endif
    head = write_at;
    tail = read_at;
#ifndef BRIDGE_HOST_TEST
    ES = saved;
#endif
    if (head >= tail) return head - tail;
    return UART1_RX_CAPACITY - tail + head;
}

uint8 Uart1_Peek(uint16 offset)
{
    uint16 pos;
    pos = read_at + offset;
    if (pos >= UART1_RX_CAPACITY) pos -= UART1_RX_CAPACITY;
    return rx[pos];
}

void Uart1_Drop(uint16 length)
{
    uint16 pos;
#ifndef BRIDGE_HOST_TEST
    uint8 saved;
#endif
    pos = read_at + length;
    if (pos >= UART1_RX_CAPACITY) pos -= UART1_RX_CAPACITY;
#ifndef BRIDGE_HOST_TEST
    saved = ES;
    ES = 0;
#endif
    read_at = pos;
#ifndef BRIDGE_HOST_TEST
    ES = saved;
#endif
}

void Uart1_ClearRx(void)
{
#ifndef BRIDGE_HOST_TEST
    uint8 saved;
    saved = ES;
    ES = 0;
#endif
    read_at = write_at;
    overflow = 0u;
#ifndef BRIDGE_HOST_TEST
    ES = saved;
#endif
}

uint8 Uart1_Overflow(void) { return overflow; }

uint8 Uart1_TxIdle(void)
{
#ifdef BRIDGE_HOST_TEST
    return Host_TxReady(1u);
#else
    return (uint8)(tx_busy == 0u);
#endif
}

uint8 Uart1_TryWrite(uint8 value)
{
    if (!Uart1_TxIdle()) return 0u;
#ifdef BRIDGE_HOST_TEST
    Host_Write(1u, value);
#else
    tx_busy = 1u;
    SBUF = value;
#endif
    return 1u;
}

#ifdef BRIDGE_HOST_TEST
void Uart1_HostReceive(uint8 value)
#else
void Uart1_Isr(void) interrupt 4 using 1
#endif
{
    uint16 next;
#ifndef BRIDGE_HOST_TEST
    uint8 value;
    if (TI) { TI = 0; tx_busy = 0u; }
    if (!RI) return;
    value = SBUF;
    RI = 0;
#endif
    /* Latch overflow and reject subsequent bytes until main explicitly resets. */
    if (overflow) return;
    next = write_at + 1u;
    if (next == UART1_RX_CAPACITY) next = 0u;
    if (next == read_at) { overflow = 1u; return; }
    rx[write_at] = value;
    write_at = next;
}
