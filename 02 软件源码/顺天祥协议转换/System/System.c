#include "Public_cfg.h"
#include "System.h"
#include "Uart1.h"
#include "Uart2.h"

/*==============================================================================
 内部定义
 *============================================================================*/

static volatile uint16 g_milliseconds;

/**
 * @brief  Timer0 内部计时中断，仅提供按键消抖所需的约 1 ms 时基
 * @param  无
 * @retval 无
 */
void Sys_Timer0Interrupt(void) interrupt 1
{
    TH0 = 0xF8u;
    TL0 = 0xCDu; // 22.1184 MHz / 12，1843 个计数约为 1 ms
    ++g_milliseconds;
}

/*==============================================================================
 对外函数
 *============================================================================*/

/**
 * @brief  初始化系统时钟分频、双串口及按键消抖时基
 * @param  无
 * @retval 无
 */
void Sys_Init(void)
{
    EA      = 0; // 初始化期间关闭总中断
    IE      = 0u;
    IE2     = 0u;
    CLK_DIV = 0u; // 系统时钟不分频
    // 内部 RC 在 ISP 设置；EXTRAM=0 使用片内 1024 字节 XRAM。
    AUXR = 0x0Du;                  // BRT 暂停、1T，串口1选 BRT，串口2倍速
    PCON = (PCON & 0xBFu) | 0x80u; // UART1 倍速且清除帧错误检测模式
    WAKE_CLKO &= 0xF8u;            // 关闭时钟输出，保留输入引脚用途
    BRT = 0xF4u;                   // 22.1184 MHz 下，两路串口固定 115200
    Uart1_Init();
    Uart2_Init();
    AUXR |= 0x10u; // 两路配置完成后启动共享波特率发生器

    TR0 = 0;
    AUXR &= 0x7Fu;                           // Timer0 使用 12T；不改变共享 BRT 的串口设置
    TMOD           = (TMOD & 0xF0u) | 0x01u; // 16 位内部计时，C/T=0、GATE=0，不占用 P3.4
    TH0            = 0xF8u;
    TL0            = 0xCDu;
    g_milliseconds = 0u;
    TF0            = 0;
    ET0            = 1; // 只启用时基，不启用外部中断或通信超时
    TR0            = 1;
    // UART1 使用高优先级，减少连续输入的接收延迟。
    IP   = 0x10u;
    IPH  = 0u;
    IP2  = 0u;
    IPH2 = 0u;
}

/**
 * @brief  短暂保护中断，避免读取到撕裂的 16 位计数
 * @param  无
 * @retval 当前毫秒计数，65536 后回绕
 */
uint16 Sys_Millis(void)
{
    uint8 enabled;
    uint16 now;
    enabled = EA;
    EA      = 0;
    now     = g_milliseconds;
    EA      = enabled;
    return now;
}
