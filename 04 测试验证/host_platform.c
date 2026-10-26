/* Host-only UART/time model. The parser, encoder and RX rings are unchanged. */
#include "Public_cfg.h"
#include "System.h"
#include "Uart1.h"
#include "Uart2.h"
#include "app_comm.h"
#include "input_control.h"

#define EXPORT __declspec(dllexport)
#define OUTPUT_MAX 262144u
static uint8 output1[OUTPUT_MAX], output2[OUTPUT_MAX];
static unsigned int length1, length2;
static unsigned long long now_us, ready1, ready2;
static unsigned int poll_us, max_rx, output_overflow;
uint8 host_p3, host_p3m0, host_p3m1;

uint8 Host_TxReady(uint8 port)
{
    return (uint8)(now_us >= (port == 1u ? ready1 : ready2));
}

void Host_Write(uint8 port, uint8 value)
{
    if (port == 1u) {
        if (length1 < OUTPUT_MAX) output1[length1++] = value;
        else output_overflow = 1u;
        ready1 = now_us + 87u;
    } else {
        if (length2 < OUTPUT_MAX) output2[length2++] = value;
        else output_overflow = 1u;
        ready2 = now_us + 87u;
    }
}

uint16 Sys_Millis(void) { return (uint16)(now_us / 1000u); }
void Sys_Init(void) { Uart1_Init(); Uart2_Init(); }

EXPORT void Host_Reset(void)
{
    now_us = ready1 = ready2 = 0u;
    length1 = length2 = max_rx = output_overflow = 0u;
    poll_us = 10u;
    host_p3 = 0xC3u;
    host_p3m0 = 0xC2u;
    host_p3m1 = 0xC1u;
    Sys_Init();
    InputControl_Init();
    AppComm_Init();
}

EXPORT void Host_SetPoll(unsigned int interval) { poll_us = interval ? interval : 1u; }
EXPORT void Host_SetInput(unsigned int channel, unsigned int active)
{
    uint8 mask;
    if (channel < 1u || channel > 4u) return;
    mask = (uint8)(1u << (channel + 1u));
    if (active) host_p3 &= (uint8)~mask;
    else host_p3 |= mask;
}
EXPORT unsigned int Host_InputRegister(unsigned int which)
{
    if (which == 0u) return host_p3;
    return which == 1u ? host_p3m0 : host_p3m1;
}
EXPORT void Host_Advance(unsigned int duration)
{
    unsigned long long end_time;
    unsigned int count;
    end_time = now_us + duration;
    while (now_us < end_time) {
        AppComm_Poll();
        count = Uart1_Count();
        if (count > max_rx) max_rx = count;
        now_us += poll_us;
    }
}
EXPORT void Host_SetMillis(unsigned int milliseconds) { now_us = (unsigned long long)milliseconds * 1000u; }
EXPORT void Host_Receive(unsigned int port, unsigned int value)
{
    unsigned int count;
    if (port == 1u) Uart1_HostReceive((uint8)value);
    else Uart2_HostReceive((uint8)value);
    count = Uart1_Count();
    if (count > max_rx) max_rx = count;
}
EXPORT unsigned int Host_Length(unsigned int port) { return port == 1u ? length1 : length2; }
EXPORT unsigned int Host_Byte(unsigned int port, unsigned int at)
{
    if (port == 1u) return at < length1 ? output1[at] : 0u;
    return at < length2 ? output2[at] : 0u;
}
EXPORT unsigned int Host_State(void) { return app_state; }
EXPORT unsigned int Host_Error(void) { return app_last_error; }
EXPORT unsigned int Host_PeakRx(void) { return max_rx; }
EXPORT unsigned int Host_OutputOverflow(void) { return output_overflow; }
