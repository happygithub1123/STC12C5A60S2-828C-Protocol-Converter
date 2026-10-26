#include "Public_cfg.h"
#include "System.h"
#include "app_comm.h"
#include "input_control.h"

/*==============================================================================
 对外函数
 *============================================================================*/

/**
 * @brief  初始化系统和输入后循环处理协议
 * @param  无
 * @retval 无
 */
void main(void)
{
    Sys_Init();
    InputControl_Init();
    AppComm_Init();
    EA = 1;
    while (1)
    {
        AppComm_Poll();
    }
}
