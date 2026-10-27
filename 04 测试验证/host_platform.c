// 仅替换串口、时间和 GPIO 寄存器；协议与环形缓冲使用实际固件源码。
#include "Public_cfg.h"
#include "System.h"
#include "Uart1.h"
#include "Uart2.h"
#include "app_comm.h"
#include "input_control.h"

/*==============================================================================
 内部定义
 *============================================================================*/

#define EXPORT     __declspec(dllexport)
#define OUTPUT_MAX 262144u
static uint8 output1[OUTPUT_MAX], output2[OUTPUT_MAX];
static unsigned int length1, length2;
static unsigned long long now_us, ready1, ready2;
static unsigned int poll_us, max_rx, output_overflow;
uint8 host_p3, host_p3m0, host_p3m1;

/*==============================================================================
 对外函数
 *============================================================================*/

/**
 * @brief  查询模拟串口是否完成发送
 * @param  port 串口编号
 * @retval 1：空闲；0：发送中
 */
uint8 Host_TxReady(uint8 port)
{
    return (uint8)(now_us >= (port == 1u ? ready1 : ready2));
}

/**
 * @brief  记录发送字节并模拟一个字符的发送时间
 * @param  port 串口编号
 * @param  value 发送字节
 * @retval 无
 */
void Host_Write(uint8 port, uint8 value)
{
    if (port == 1u)
    {
        if (length1 < OUTPUT_MAX)
        {
            output1[length1++] = value;
        }
        else
        {
            output_overflow = 1u;
        }
        ready1 = now_us + 87u;
    }
    else
    {
        if (length2 < OUTPUT_MAX)
        {
            output2[length2++] = value;
        }
        else
        {
            output_overflow = 1u;
        }
        ready2 = now_us + 87u;
    }
}

/**
 * @brief  初始化同一份串口驱动的接收缓冲
 * @param  无
 * @retval 无
 */
void Sys_Init(void)
{
    Uart1_Init();
    Uart2_Init();
}

/**
 * @brief  提供与固件接口相同的 16 位模拟毫秒计数
 * @param  无
 * @retval 当前模拟毫秒，65536 后回绕
 */
uint16 Sys_Millis(void)
{
    return (uint16)(now_us / 1000u);
}

/**
 * @brief  复位测试模型和固件会话
 * @param  无
 * @retval 无
 */
EXPORT void Host_Reset(void)
{
    now_us = ready1 = ready2 = 0u;
    length1 = length2 = max_rx = output_overflow = 0u;
    poll_us                                      = 10u;
    host_p3                                      = 0xC3u;
    host_p3m0                                    = 0xC2u;
    host_p3m1                                    = 0xC1u;
    Sys_Init();
    InputControl_Init();
    AppComm_Init();
}

/**
 * @brief  设置主循环调度间隔
 * @param  interval 间隔，单位微秒
 * @retval 无
 */
EXPORT void Host_SetPoll(unsigned int interval)
{
    poll_us = interval ? interval : 1u;
}
/**
 * @brief  设置模拟输入电平
 * @param  channel 通道 1～5
 * @param  active 非零表示低电平有效
 * @retval 无
 */
EXPORT void Host_SetInput(unsigned int channel, unsigned int active)
{
    uint8 mask;
    if (channel < 1u || channel > 5u)
    {
        return;
    }
    mask = (uint8)(1u << (channel + 1u));
    if (active)
    {
        host_p3 &= (uint8)~mask;
    }
    else
    {
        host_p3 |= mask;
    }
}
/**
 * @brief  查看模拟端口寄存器
 * @param  which 0：P3；1：P3M0；2：P3M1
 * @retval 寄存器值
 */
EXPORT unsigned int Host_InputRegister(unsigned int which)
{
    if (which == 0u)
    {
        return host_p3;
    }
    return which == 1u ? host_p3m0 : host_p3m1;
}
/**
 * @brief  推进模拟时间并轮询固件
 * @param  duration 时间，单位微秒
 * @retval 无
 */
EXPORT void Host_Advance(unsigned int duration)
{
    unsigned long long end_time;
    unsigned int count;
    end_time = now_us + duration;
    while (now_us < end_time)
    {
        AppComm_Poll();
        count = Uart1_Count();
        if (count > max_rx)
        {
            max_rx = count;
        }
        now_us += poll_us;
    }
}
/**
 * @brief  设置模拟时间，验证长时间等待不受旧计时回绕影响
 * @param  milliseconds 毫秒计数
 * @retval 无
 */
EXPORT void Host_SetMillis(unsigned int milliseconds)
{
    now_us = (unsigned long long)milliseconds * 1000u;
}
/**
 * @brief  向实际环形缓冲注入模拟串口字节
 * @param  port 串口编号
 * @param  value 接收字节
 * @retval 无
 */
EXPORT void Host_Receive(unsigned int port, unsigned int value)
{
    unsigned int count;
    if (port == 1u)
    {
        Uart1_HostReceive((uint8)value);
    }
    else
    {
        Uart2_HostReceive((uint8)value);
    }
    count = Uart1_Count();
    if (count > max_rx)
    {
        max_rx = count;
    }
}
/**
 * @brief  获取已捕获的串口输出长度
 * @param  port 串口编号
 * @retval 字节数
 */
EXPORT unsigned int Host_Length(unsigned int port)
{
    return port == 1u ? length1 : length2;
}
/**
 * @brief  读取已捕获输出的指定字节
 * @param  port 串口编号
 * @param  at 字节索引
 * @retval 字节值，越界返回 0
 */
EXPORT unsigned int Host_Byte(unsigned int port, unsigned int at)
{
    if (port == 1u)
    {
        return at < length1 ? output1[at] : 0u;
    }
    return at < length2 ? output2[at] : 0u;
}
/**
 * @brief  查询固件会话状态
 * @param  无
 * @retval 会话状态编号
 */
EXPORT unsigned int Host_State(void)
{
    return app_state;
}
/**
 * @brief  查询固件调试错误码
 * @param  无
 * @retval 错误码
 */
EXPORT unsigned int Host_Error(void)
{
    return app_last_error;
}
/**
 * @brief  查询模型观察到的接收缓冲峰值
 * @param  无
 * @retval 字节数
 */
EXPORT unsigned int Host_PeakRx(void)
{
    return max_rx;
}
/**
 * @brief  查询测试输出捕获区是否溢出
 * @param  无
 * @retval 1：溢出；0：正常
 */
EXPORT unsigned int Host_OutputOverflow(void)
{
    return output_overflow;
}
