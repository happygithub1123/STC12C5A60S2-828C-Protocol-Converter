#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// 内部 RC 固定 22.1184 MHz；两路串口固定 115200、8位数据、无校验、1位停止。
#define PROJECT_FOSC_HZ    22118400UL

#define FIXTURE_DEFAULT_ID 1u
#define UART1_RX_CAPACITY  768u
#define UART2_RX_CAPACITY  64u
#define INPUT_LINE_MAX     511u
#define PROJECT_NAME_MAX   31u
#define PROJECT_UNIT_MAX   8u
#define COMMAND_MAX        23u
#define FIRST_RESPONSE_MS  3000u
#define INPUT_WAIT_MS      1000u

#if FIXTURE_DEFAULT_ID < 1 || FIXTURE_DEFAULT_ID > 4
#error FIXTURE_DEFAULT_ID_must_be_1_to_4
#endif
#if UART1_RX_CAPACITY <= (INPUT_LINE_MAX + 2)
#error UART1_ring_must_hold_a_complete_line
#endif

// Timer0 仅用于通信超时，按固定时钟产生约 1 ms 计数。
#define TIMER0_TICKS  ((PROJECT_FOSC_HZ + 6000UL) / 12000UL)
#define TIMER0_RELOAD (65536UL - TIMER0_TICKS)

#endif
