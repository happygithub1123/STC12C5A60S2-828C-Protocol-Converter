#include "Public_cfg.h"
#include "System.h"
#include "app_comm.h"

void main(void)
{
    Sys_Init();
    AppComm_Init();
    EA = 1;
    while (1) 
    {
        AppComm_Poll();
    }
}
