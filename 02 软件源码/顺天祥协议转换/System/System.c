#include "Public_cfg.h"
#include "System.h"
#include "Uart1.h"
#include "Uart2.h"

static volatile uint16 milliseconds;

void Sys_Init(void)
{
    EA = 0;
    IE = 0u;
    IE2 = 0u;
    CLK_DIV = 0u;
    /* Internal RC is selected in ISP, not with STC8 clock registers.
     * EXTRAM=0 enables the STC12's internal 1024-byte XRAM.
     * BRT stopped during setup; both UARTs select BRT, 1T, x2 baud. */
    AUXR = 0x0Du;
    PCON = (PCON & 0xBFu) | 0x80u;
    WAKE_CLKO &= 0xF8u;
    BRT = (uint8)BRT_RELOAD;
    Uart1_Init();
    Uart2_Init();
    AUXR |= 0x10u;

    milliseconds = 0u;
    TMOD = 0x01u; /* Timer0 16-bit, 12T; only used for communication timeout. */
    TH0 = (uint8)(TIMER0_RELOAD >> 8);
    TL0 = (uint8)TIMER0_RELOAD;
    TF0 = 0;
    ET0 = 1;
    TR0 = 1;
    /* UART1 can preempt Timer0 to keep the RX interrupt short at 115200. */
    IP = 0x10u;
    IPH = 0u;
    IP2 = 0u;
    IPH2 = 0u;
}

uint16 Sys_Millis(void)
{
    uint8 saved;
    uint16 value;
    saved = EA;
    EA = 0;
    value = milliseconds;
    EA = saved;
    return value;
}

void Timer0_Isr(void) interrupt 1 using 3
{
    TH0 = (uint8)(TIMER0_RELOAD >> 8);
    TL0 = (uint8)TIMER0_RELOAD;
    ++milliseconds;
}
