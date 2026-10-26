#ifndef PROJECT_TYPES_H
#define PROJECT_TYPES_H

#ifdef BRIDGE_HOST_TEST
#include <stdint.h>
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
#define XDATA
#define IDATA
#define CODE
#else
typedef unsigned char uint8;
typedef unsigned int uint16;
typedef unsigned long uint32;
#define XDATA xdata
#define IDATA idata
#define CODE code
#endif

#endif
