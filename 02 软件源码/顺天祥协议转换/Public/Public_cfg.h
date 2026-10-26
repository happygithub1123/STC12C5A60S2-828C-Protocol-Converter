#ifndef PUBLIC_CFG_H
#define PUBLIC_CFG_H

// C51 使用 8 位 char、16 位 int；主机测试显式使用相同宽度。
#ifdef BRIDGE_HOST_TEST
#include <stdint.h>
typedef uint8_t uint8;
typedef uint16_t uint16;
#define XDATA
#define IDATA
#define CODE
#else
typedef unsigned char uint8;
typedef unsigned int uint16;
#define XDATA xdata
#define IDATA idata
#define CODE  code
#include "STC12C5A60S2.h"
#endif

#include "board_config.h"
#endif
