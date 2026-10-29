#include "input_control.h"

/*==============================================================================
 内部定义
 *============================================================================*/

#define INPUT_PIN_MASK 0x7Cu

/*==============================================================================
 对外函数
 *============================================================================*/

/**
 * @brief  初始化 X1～X5 输入，保留同端口其他引脚配置
 * @param  无
 * @retval 无
 */
void InputControl_Init(void)
{
    P3M0 &= 0x83u; // P3.2～P3.6 使用准双向模式
    P3M1 &= 0x83u;
    P3 |= INPUT_PIN_MASK; // 写 1 释放输入并启用内部弱上拉
}

/**
 * @brief  一次读取五路低电平输入，供模号识别和 X5 触发共用
 * @param  无
 * @retval 位 0～4 分别对应 X1～X5，置 1 表示低电平有效
 */
uint8 InputControl_ReadActive(void)
{
    uint8 port;

    port = P3; // 只读取一次端口，避免逐路读取造成模号混合
    return (uint8)(((uint8)~port & INPUT_PIN_MASK) >> 2);
}

/**
 * @brief  从同一次输入快照识别唯一有效模号
 * @param  inputs InputControl_ReadActive 返回的输入位图
 * @retval 1～4：唯一有效模号；0：无模号或多路同时有效
 */
uint8 InputControl_GetModule(uint8 inputs)
{
    switch (inputs & 0x0Fu)
    {
        case 0x01u:
            return 1u;
        case 0x02u:
            return 2u;
        case 0x04u:
            return 3u;
        case 0x08u:
            return 4u;
        default:
            return 0u;
    }
}
