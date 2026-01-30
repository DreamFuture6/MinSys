/**
 * @name    MinSys
 * @author  DreamFuture6
 * @version 0.1.2
 * @date    2026/1/30
 * @brief   A soft real-time operating system for embedded hardware with low resource consumption.
 *
 *            Task-Supported Operations Table
 *   ┌─────────────┬───────┬───────┬─────────┬───────┐   ┌───────────────────┐
 *   │  TASK TYPE  │ YIELD │ DELAY │ SUSPEND │ CLOSE │   │  Main Func Param  │
 *   ├─────────────┼───────┼───────┼─────────┼───────┤   ├───────────────────┤
 *   │ circle task │   √   │   √   │    √    │   √   │   │  (count,  state)  │
 *   │ single task │   √   │   √   │    ×    │   √   │   │  (0,      state)  │
 *   │ events task │   ×   │   √   │    √    │   √   │   │  (value, signal)  │  Note: When the delay ends, the parameter is (0,0)
 *   └─────────────┴───────┴───────┴─────────┴───────┘   └───────────────────┘
 **/
#ifndef __SYSTEM_CORE_H
#define __SYSTEM_CORE_H

#include "SystemConfig.h"

#define SYS_PRINT_MSG(x) "[MinSys] " x
#if EVENT_MAX_NUM < 0
#error "'EVENT_MAX_NUM' must be a nonnegative integer (>=0)!"
#elif EVENT_MAX_NUM == 0
#pragma message(SYS_PRINT_MSG("Disable Function: event task."))
#if TASK_MAX_NUM <= 0
#error "'TASK_MAX_NUM' must be a positive integer (>=1)!"
#endif
#ifdef AUTO_SLEEP
#error "The 'AUTO_SLEEP' function requires enabling event task feature, meaning the 'TASK_MAX_NUM' must be a positive integer (>=1)!"
#endif
#else
#if TASK_MAX_NUM <= 1
#error "Because a system management thread exists, the value of 'TASK_MAX_NUM' must be greater than 1 (>=2)!"
#endif
#define ENABLE_EVENT_TASK
#endif
#if TASK_MAX_NUM > 65535
#error "'TASK_MAX_NUM' is too large (>65535)!"
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef void (*TaskMainFunc)(u32, u16); // task main function
typedef struct Task Task;               // task handle
#ifdef ENABLE_EVENT_TASK
typedef struct Event Event; // event handle
#endif

#ifndef __cplusplus
#define NULL  ((void *)0)
#define bool  _Bool
#define false 0
#define true  1
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* Pending Interface */
u32 System_GetCurrTick(void);
#ifdef AUTO_SLEEP
void System_Sleep(void);
#endif

/* System Function  */
void System_Init(void);
void System_Loop(void);
void System_EndLoop(void);
#ifdef IDLE_HOOK_FUNCITON
void System_RegisterIdleTask(TaskMainFunc func);
#endif

/* Task Creation Function */
Task *System_AddNewLoopTask(TaskMainFunc func, u32 interval);
Task *System_AddNewTempTask(TaskMainFunc func, u32 interval);
#ifdef ENABLE_EVENT_TASK
Task *System_AddNewEventTask(TaskMainFunc func, Event *event, u16 signal);
#endif

/* Global Task Operation Function */
bool System_SuspendTask(Task *task, u16 nextState);
bool System_ResumeTask(Task *task, u16 execState, bool instance);
bool System_KillTask(Task *task);

#ifdef ENABLE_EVENT_TASK
/* Event Task Operation Function  */
Event *System_CreateEvent(void);
bool System_DeleteEvent(Event *event);
bool System_SetEvent(Event *event, u16 signal, u32 value);
u16 System_GetEventSignal(Event *event);
#endif

/* Current Task Operation Function */
bool Task_Yield(u16 nextState);
bool Task_Delay(u16 ticks, u16 nextState);
bool Task_Suspend(u16 nextState);
#ifdef ENABLE_EVENT_TASK
bool Task_ListenSingal(u16 newSignal);
#endif
void Task_Close(void);
#ifdef __cplusplus
}
#endif
#endif