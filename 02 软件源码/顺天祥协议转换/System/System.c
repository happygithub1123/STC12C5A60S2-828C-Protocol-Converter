#include "Public_cfg.h"
#include "System.h"
#include "Uart1.h"
#include "Uart2.h"

/*==============================================================================
 内部定义
 *============================================================================*/

static volatile uint16 g_system_ms;

/*==============================================================================
 对外函数
 *============================================================================*/

/**
 * @brief  初始化系统时钟分频、双串口和通信计时
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

    g_system_ms = 0u;
    TMOD        = 0x01u; // Timer0 使用 16 位、12T 模式，只用于通信计时
    TH0         = (uint8)(TIMER0_RELOAD >> 8);
    TL0         = (uint8)TIMER0_RELOAD;
    TF0         = 0;
    ET0         = 1;
    TR0         = 1;
    // UART1 可抢占计时中断，减少 115200 接收延迟。
    IP   = 0x10u;
    IPH  = 0u;
    IP2  = 0u;
    IPH2 = 0u;
}

/**
 * @brief  原子读取允许自然回绕的毫秒计数
 * @param  无
 * @retval 当前 16 位毫秒计数
 */
uint16 Sys_Millis(void)
{
    uint8 saved;
    uint16 value;
    saved = EA;
    EA    = 0; // 防止 16 位计数读取一半时被中断改写
    value = g_system_ms;
    EA    = saved;
    return value;
}

/**
 * @brief  定时器0中断维护通信毫秒计数
 * @param  无
 * @retval 无
 */
void Timer0_Isr(void) interrupt 1 using 3
{
    TH0 = (uint8)(TIMER0_RELOAD >> 8);
    TL0 = (uint8)TIMER0_RELOAD;
    ++g_system_ms;
}
