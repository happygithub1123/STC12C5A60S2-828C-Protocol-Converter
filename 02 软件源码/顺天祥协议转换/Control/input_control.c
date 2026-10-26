#include "input_control.h"

/*==============================================================================
 内部定义
 *============================================================================*/

#define INPUT_PIN_MASK 0x3Cu

#ifdef BRIDGE_HOST_TEST
extern uint8 host_p3, host_p3m0, host_p3m1;
#define INPUT_PORT host_p3
#define INPUT_MODE0 host_p3m0
#define INPUT_MODE1 host_p3m1
#else
#define INPUT_PORT P3
#define INPUT_MODE0 P3M0
#define INPUT_MODE1 P3M1
#endif

/*==============================================================================
 对外函数
 *============================================================================*/

/**
 * @brief  初始化 X1～X4 输入，保留同端口其他引脚配置
 * @param  无
 * @retval 无
 */
void InputControl_Init(void)
{
    INPUT_MODE0 &= 0xC3u;                 // P3.2～P3.5 使用准双向模式
    INPUT_MODE1 &= 0xC3u;
    INPUT_PORT |= INPUT_PIN_MASK;         // 写 1 释放输入并启用内部弱上拉
}

/**
 * @brief  读取指定启动输入是否有效
 * @param  channel 通道编号，范围为 1～4
 * @retval 1：低电平有效；0：高电平或通道非法
 */
uint8 InputControl_IsActive(uint8 channel)
{
    uint8 mask;

    if (channel < 1u || channel > 4u)
    {
        return 0u;
    }
    mask = (uint8)(1u << (channel + 1u));
    return (uint8)((INPUT_PORT & mask) == 0u);
}
