#include "Public_cfg.h"
#include "Uart2.h"

/*==============================================================================
 内部定义
 *============================================================================*/

static volatile uint8 XDATA g_uart2_rx_buffer[UART2_RX_CAPACITY];
static volatile uint8 g_uart2_read_index, g_uart2_write_index, g_uart2_rx_overflow;
static volatile uint8 g_uart2_tx_busy;

/*==============================================================================
 对外函数
 *============================================================================*/

/**
 * @brief  初始化串口2接收缓冲和硬件
 * @param  无
 * @retval 无
 */
void Uart2_Init(void)
{
    g_uart2_read_index  = 0u;
    g_uart2_write_index = 0u;
    g_uart2_rx_overflow = 0u;
    // UART2 选择 P1.2 接收、P1.3 推挽发送。
    AUXR1 &= 0xEFu; // 串口2使用默认 P1.2/P1.3 引脚
    P1ASF &= 0xF3u; // 关闭对应引脚模拟功能
    P1M1 = (P1M1 & 0xF3u) | 0x04u;
    P1M0 = (P1M0 & 0xF3u) | 0x08u;
    P1 |= 0x0Cu;
    S2CON           = 0x50u; // 8位数据、无校验、1位停止，允许接收
    g_uart2_tx_busy = 0u;
    IE2 |= 0x01u;
}

/**
 * @brief  读取一个上位机接收字节
 * @param  value 接收字节的存放地址，调用方保证有效
 * @retval 1：成功；0：缓冲为空
 */
uint8 Uart2_ReadByte(uint8 *value)
{
    uint8 next;
    if (g_uart2_read_index == g_uart2_write_index)
    {
        return 0u;
    }
    *value = g_uart2_rx_buffer[g_uart2_read_index];
    next   = g_uart2_read_index + 1u;
    if (next == UART2_RX_CAPACITY)
    {
        next = 0u;
    }
    g_uart2_read_index = next; // 以一次 8 位写入发布索引，避免中断看到中间值
    return 1u;
}

/**
 * @brief  原子丢弃接收内容并解除溢出状态
 * @param  无
 * @retval 无
 */
void Uart2_ClearRx(void)
{
    uint8 saved;
    saved = IE2 & 0x01u;
    IE2 &= 0xFEu;
    g_uart2_read_index  = g_uart2_write_index;
    g_uart2_rx_overflow = 0u;
    IE2 |= saved;
}

/**
 * @brief  查询接收溢出是否已锁存
 * @param  无
 * @retval 1：溢出；0：正常
 */
uint8 Uart2_Overflow(void)
{
    return g_uart2_rx_overflow;
}
/**
 * @brief  查询发送器是否可接受下一字节
 * @param  无
 * @retval 1：空闲；0：忙
 */
uint8 Uart2_TxIdle(void)
{
    return (uint8)(g_uart2_tx_busy == 0u);
}

/**
 * @brief  非阻塞发送一个字节
 * @param  value 待发送字节
 * @retval 1：已交给硬件；0：发送忙
 */
uint8 Uart2_TryWrite(uint8 value)
{
    if (!Uart2_TxIdle())
    {
        return 0u;
    }
    g_uart2_tx_busy = 1u;
    S2BUF           = value;
    return 1u;
}

/**
 * @brief  处理发送完成与接收字节，不解析协议
 * @param  无
 * @retval 无
 */
void Uart2_Isr(void) interrupt 8 using 2
{
    uint8 next;
    uint8 value;
    if (S2CON & 0x02u)
    {
        S2CON &= 0xFDu;
        g_uart2_tx_busy = 0u;
    }
    if (!(S2CON & 0x01u))
    {
        return;
    }
    value = S2BUF;
    S2CON &= 0xFEu;
    if (g_uart2_rx_overflow)
    {
        return;
    }
    next = g_uart2_write_index + 1u;
    if (next == UART2_RX_CAPACITY)
    {
        next = 0u;
    }
    if (next == g_uart2_read_index)
    {
        g_uart2_rx_overflow = 1u;
        return;
    }
    g_uart2_rx_buffer[g_uart2_write_index] = value;
    g_uart2_write_index                    = next;
}
