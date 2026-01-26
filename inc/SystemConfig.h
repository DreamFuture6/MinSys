#ifndef __SYSTEM_CONFIG_H
#define __SYSTEM_CONFIG_H

/* Core Configuration */
#include <stdint.h>      // device header file
#define TASK_MAX_NUM  16 // value ≥ 1
#define EVENT_MAX_NUM 8  // value ≥ 0

/* Optional Features */
// #define IDLE_HOOK_FUNCITON // Execute during idle time slots [void (currIdleTick, lastIdleTick)]
// #define AUTO_SLEEP         // Only effective in the full event-driven framework

/* Plugins */
// New features are in development...

#endif