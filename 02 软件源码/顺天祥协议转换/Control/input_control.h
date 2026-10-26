#ifndef INPUT_CONTROL_H
#define INPUT_CONTROL_H

#include "Public_cfg.h"

void InputControl_Init(void);               /* 初始化 X1～X4 准双向输入。 */
uint8 InputControl_IsActive(uint8 channel); /* 指定通道为低时返回 1，非法通道返回 0。 */

#endif
