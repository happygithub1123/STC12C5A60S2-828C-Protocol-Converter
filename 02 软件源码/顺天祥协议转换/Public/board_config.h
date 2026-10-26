#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Internal RC frequency selected in STC-ISP: 22.1184 MHz.
 * BRT divisor 12 -> baud 115200 exactly (0% error).
 * BRIDGE_BUILD_TEST still overrides to an 11.0592 MHz build-check clock.
 */
#ifndef PROJECT_FOSC_HZ
#define PROJECT_FOSC_HZ 22118400UL
#endif
#if defined(BRIDGE_BUILD_TEST) && (PROJECT_FOSC_HZ == 0)
#undef PROJECT_FOSC_HZ
#define PROJECT_FOSC_HZ 11059200UL
#endif

#define UART_BAUD                 115200UL
#define FIXTURE_DEFAULT_ID        1u
#define UART1_RX_CAPACITY         768u
#define UART2_RX_CAPACITY         64u
#define INPUT_LINE_MAX            511u
#define PROJECT_NAME_MAX          31u
#define PROJECT_UNIT_MAX          8u
#define COMMAND_MAX              23u
#define FIRST_RESPONSE_MS         3000u
#define INPUT_WAIT_MS             1000u

#if FIXTURE_DEFAULT_ID < 1 || FIXTURE_DEFAULT_ID > 4
#error FIXTURE_DEFAULT_ID_must_be_1_to_4
#endif
#if UART1_RX_CAPACITY <= (INPUT_LINE_MAX + 2)
#error UART1_ring_must_hold_a_complete_line
#endif

#ifndef BRIDGE_HOST_TEST
#if PROJECT_FOSC_HZ < 1000000UL || PROJECT_FOSC_HZ > 35000000UL
#error Set_PROJECT_FOSC_HZ_to_ISP_measured_internal_RC_frequency
#else
/* Both UARTs use BRT, 1T, SMOD/S2SMOD=1: baud = Fosc/(16*N). */
#define BRT_DIVISOR ((PROJECT_FOSC_HZ + UART_BAUD * 8UL) / (UART_BAUD * 16UL))
#define BRT_RELOAD  (256UL - BRT_DIVISOR)
#define ACTUAL_BAUD (PROJECT_FOSC_HZ / (16UL * BRT_DIVISOR))
#if BRT_DIVISOR < 1 || BRT_DIVISOR > 255
#error Unsupported_BRT_divisor
#endif
#if ACTUAL_BAUD > (UART_BAUD * 102UL / 100UL) || ACTUAL_BAUD < (UART_BAUD * 98UL / 100UL)
#error Internal_RC_frequency_cannot_generate_115200_within_2_percent
#endif
#define TIMER0_TICKS ((PROJECT_FOSC_HZ + 6000UL) / 12000UL)
#define TIMER0_RELOAD (65536UL - TIMER0_TICKS)
#endif
#endif

#endif
