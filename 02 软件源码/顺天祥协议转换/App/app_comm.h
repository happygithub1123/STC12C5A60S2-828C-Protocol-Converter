#ifndef APP_COMM_H
#define APP_COMM_H
#include "Public_cfg.h"

#define APP_IDLE                0u
#define APP_SENDING_START       1u
#define APP_WAIT_FIRST          2u
#define APP_RECEIVING           3u
#define APP_WAIT_INPUT          4u

#define APP_ERROR_NONE          0u
#define APP_ERROR_COMMAND       1u
#define APP_ERROR_FORMAT        2u
#define APP_ERROR_LINE_LONG     3u
#define APP_ERROR_UART1_FULL    4u
#define APP_ERROR_UART2_FULL    5u
#define APP_ERROR_FIRST_TIMEOUT 6u
#define APP_ERROR_INPUT_TIMEOUT 7u

void AppComm_Init(void); /* 初始化协议状态与接收缓冲。 */
void AppComm_Poll(void); /* 非阻塞处理命令、触发、记录和发送。 */
// 仅供调试器查看，不写入协议串口。
extern uint8 app_state;
extern uint8 app_last_error;

#endif
