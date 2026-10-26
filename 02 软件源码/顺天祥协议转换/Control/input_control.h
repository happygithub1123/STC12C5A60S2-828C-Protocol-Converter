#ifndef INPUT_CONTROL_H
#define INPUT_CONTROL_H

#include "Public_cfg.h"

void InputControl_Init(void);               /* 初始化 X1～X5 准双向输入。 */
uint8 InputControl_ReadActive(void);        /* 一次读取端口，位 0～4 对应 X1～X5。 */
uint8 InputControl_GetModule(uint8 inputs); /* 从快照识别唯一模号，无效返回 0。 */

#endif
