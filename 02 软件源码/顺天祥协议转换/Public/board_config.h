#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// 内部 RC 固定 22.1184 MHz；两路串口固定 115200、8位数据、无校验、1位停止。
#define PROJECT_FOSC_HZ   22118400UL

#define UART1_RX_CAPACITY 768u
#define UART2_RX_CAPACITY 64u
#define INPUT_LINE_MAX    511u
#define PROJECT_NAME_MAX  31u
#define PROJECT_UNIT_MAX  8u
#define COMMAND_MAX       23u
#if UART1_RX_CAPACITY <= (INPUT_LINE_MAX + 2)
#error UART1_ring_must_hold_a_complete_line
#endif

#endif
