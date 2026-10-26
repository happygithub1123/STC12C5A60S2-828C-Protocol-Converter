#include "Public_cfg.h"
#include "Uart1.h"

/*==============================================================================
 内部定义
 *============================================================================*/

static volatile uint8 XDATA g_uart1_rx_buffer[UART1_RX_CAPACITY];
static volatile uint16 g_uart1_read_index, g_uart1_write_index;
static volatile uint8 g_uart1_rx_overflow;
#ifndef BRIDGE_HOST_TEST
static volatile uint8 g_uart1_tx_busy;
#else
extern uint8 Host_TxReady(uint8 port);
extern void Host_Write(uint8 port, uint8 value);
#endif

/*==============================================================================
 对外函数
 *============================================================================*/

/**
 * @brief  初始化串口1接收缓冲和硬件
 * @param  无
 * @retval 无
 */
void Uart1_Init(void)
{
    g_uart1_read_index  = 0u;
    g_uart1_write_index = 0u;
    g_uart1_rx_overflow = 0u;
#ifndef BRIDGE_HOST_TEST
    // UART1 固定使用 P3.0 接收、P3.1 推挽发送。
    P3M1 = (P3M1 & 0xFCu) | 0x01u;
    P3M0 = (P3M0 & 0xFCu) | 0x02u;
    P3 |= 0x03u;
    SCON            = 0x50u; // 模式1、8N1，允许接收并清除 RI/TI
    g_uart1_tx_busy = 0u;
    ES              = 1;
#endif
}

/**
 * @brief  原子获取接收字节数快照
 * @param  无
 * @retval 当前可读字节数
 */
uint16 Uart1_Count(void)
{
    uint16 head, tail;
#ifndef BRIDGE_HOST_TEST
    uint8 saved;
    saved = ES;
    ES    = 0; // 16 位读写索引需在 UART1 临界区取得快照
#endif
    head = g_uart1_write_index;
    tail = g_uart1_read_index;
#ifndef BRIDGE_HOST_TEST
    ES = saved;
#endif
    if (head >= tail)
    {
        return head - tail;
    }
    return UART1_RX_CAPACITY - tail + head;
}

/**
 * @brief  查看快照内的接收字节，不移动索引
 * @param  offset 从当前读位置开始的偏移，必须小于 Count 快照
 * @retval 指定字节
 */
uint8 Uart1_Peek(uint16 offset)
{
    uint16 pos;
    pos = g_uart1_read_index + offset;
    if (pos >= UART1_RX_CAPACITY)
    {
        pos -= UART1_RX_CAPACITY;
    }
    return g_uart1_rx_buffer[pos];
}

/**
 * @brief  释放已校验且已消费的接收字节
 * @param  length 要释放的字节数，不超过当前可读量
 * @retval 无
 */
void Uart1_Drop(uint16 length)
{
    uint16 pos;
#ifndef BRIDGE_HOST_TEST
    uint8 saved;
#endif
    pos = g_uart1_read_index + length;
    if (pos >= UART1_RX_CAPACITY)
    {
        pos -= UART1_RX_CAPACITY;
    }
#ifndef BRIDGE_HOST_TEST
    saved = ES;
    ES    = 0;
#endif
    g_uart1_read_index = pos;
#ifndef BRIDGE_HOST_TEST
    ES = saved;
#endif
}

/**
 * @brief  原子丢弃接收内容并解除溢出状态
 * @param  无
 * @retval 无
 */
void Uart1_ClearRx(void)
{
#ifndef BRIDGE_HOST_TEST
    uint8 saved;
    saved = ES;
    ES    = 0;
#endif
    g_uart1_read_index  = g_uart1_write_index;
    g_uart1_rx_overflow = 0u;
#ifndef BRIDGE_HOST_TEST
    ES = saved;
#endif
}

/**
 * @brief  查询接收溢出是否已锁存
 * @param  无
 * @retval 1：溢出；0：正常
 */
uint8 Uart1_Overflow(void)
{
    return g_uart1_rx_overflow;
}

/**
 * @brief  查询发送器是否可接受下一字节
 * @param  无
 * @retval 1：空闲；0：忙
 */
uint8 Uart1_TxIdle(void)
{
#ifdef BRIDGE_HOST_TEST
    return Host_TxReady(1u);
#else
    return (uint8)(g_uart1_tx_busy == 0u);
#endif
}

/**
 * @brief  非阻塞发送一个字节
 * @param  value 待发送字节
 * @retval 1：已交给硬件；0：发送忙
 */
uint8 Uart1_TryWrite(uint8 value)
{
    if (!Uart1_TxIdle())
    {
        return 0u;
    }
#ifdef BRIDGE_HOST_TEST
    Host_Write(1u, value);
#else
    g_uart1_tx_busy = 1u;
    SBUF            = value;
#endif
    return 1u;
}

#ifdef BRIDGE_HOST_TEST
/**
 * @brief  主机测试模拟接收中断入口
 * @param  value 模拟收到的字节
 * @retval 无
 */
void Uart1_HostReceive(uint8 value)
#else
/**
 * @brief  处理发送完成与接收字节，不解析协议
 * @param  无
 * @retval 无
 */
void Uart1_Isr(void) interrupt 4 using 1
#endif
{
    uint16 next;
#ifndef BRIDGE_HOST_TEST
    uint8 value;
    if (TI)
    {
        TI              = 0;
        g_uart1_tx_busy = 0u;
    }
    if (!RI)
    {
        return;
    }
    value = SBUF;
    RI    = 0;
#endif
    // 溢出后拒收新字节，等待主循环清除，避免覆盖正在编码的数据。
    if (g_uart1_rx_overflow)
    {
        return;
    }
    next = g_uart1_write_index + 1u;
    if (next == UART1_RX_CAPACITY)
    {
        next = 0u;
    }
    if (next == g_uart1_read_index)
    {
        g_uart1_rx_overflow = 1u;
        return;
    }
    g_uart1_rx_buffer[g_uart1_write_index] = value;
    g_uart1_write_index                    = next;
}
