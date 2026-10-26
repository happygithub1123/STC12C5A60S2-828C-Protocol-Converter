#include "Public_cfg.h"
#include "Uart2.h"

static volatile uint8 XDATA rx[UART2_RX_CAPACITY];
static volatile uint8 read_at, write_at, overflow;
#ifndef BRIDGE_HOST_TEST
static volatile uint8 tx_busy;
#else
extern uint8 Host_TxReady(uint8 port);
extern void Host_Write(uint8 port, uint8 value);
#endif

void Uart2_Init(void)
{
    read_at = 0u;
    write_at = 0u;
    overflow = 0u;
#ifndef BRIDGE_HOST_TEST
    /* STC12 UART2: AUXR1.S2_P4=0 -> P1.2 RX / P1.3 TX. */
    AUXR1 &= 0xEFu;
    P1ASF &= 0xF3u;
    P1M1 = (P1M1 & 0xF3u) | 0x04u;
    P1M0 = (P1M0 & 0xF3u) | 0x08u;
    P1 |= 0x0Cu;
    S2CON = 0x50u;
    tx_busy = 0u;
    IE2 |= 0x01u;
#endif
}

uint8 Uart2_ReadByte(uint8 *value)
{
    uint8 next;
    if (read_at == write_at) return 0u;
    *value = rx[read_at];
    next = read_at + 1u;
    if (next == UART2_RX_CAPACITY) next = 0u;
    read_at = next;
    return 1u;
}

void Uart2_ClearRx(void)
{
#ifndef BRIDGE_HOST_TEST
    uint8 saved;
    saved = IE2 & 0x01u;
    IE2 &= 0xFEu;
#endif
    read_at = write_at;
    overflow = 0u;
#ifndef BRIDGE_HOST_TEST
    IE2 |= saved;
#endif
}

uint8 Uart2_Overflow(void) { return overflow; }
uint8 Uart2_TxIdle(void)
{
#ifdef BRIDGE_HOST_TEST
    return Host_TxReady(2u);
#else
    return (uint8)(tx_busy == 0u);
#endif
}

uint8 Uart2_TryWrite(uint8 value)
{
    if (!Uart2_TxIdle()) return 0u;
#ifdef BRIDGE_HOST_TEST
    Host_Write(2u, value);
#else
    tx_busy = 1u;
    S2BUF = value;
#endif
    return 1u;
}

#ifdef BRIDGE_HOST_TEST
void Uart2_HostReceive(uint8 value)
#else
void Uart2_Isr(void) interrupt 8 using 2
#endif
{
    uint8 next;
#ifndef BRIDGE_HOST_TEST
    uint8 value;
    if (S2CON & 0x02u) { S2CON &= 0xFDu; tx_busy = 0u; }
    if (!(S2CON & 0x01u)) return;
    value = S2BUF;
    S2CON &= 0xFEu;
#endif
    if (overflow) return;
    next = write_at + 1u;
    if (next == UART2_RX_CAPACITY) next = 0u;
    if (next == read_at) { overflow = 1u; return; }
    rx[write_at] = value;
    write_at = next;
}
