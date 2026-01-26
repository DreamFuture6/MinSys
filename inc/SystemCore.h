/**
 * @name    MinSys
 * @author  DreamFuture6
 * @version 0.1.0
 * @date    2026/1/26
 * @brief   A soft real-time operating system for embedded hardware with low resource consumption.
 *
 *        Task-Supported Operations Table
 *   ┌─────────────┬───────┬─────────┬───────┐   ┌───────────────────┐
 *   │  TASK TYPE  │ DELAY │ SUSPEND │ CLOSE │   │  Main Func Param  │
 *   ├─────────────┼───────┼─────────┼───────┤   ├───────────────────┤
 *   │ circle task │   √   │    √    │   √   │   │  (count,  info )  │    DELAY:   The highest bit of "info" is 0.
 *   │ single task │   √   │    ×    │   √   │   │  (0,      info )  │    SUSPEND: The highest bit of "info" is 1.
 *   │ events task │   ×   │    √    │   √   │   │  (signal, value)  │
 *   └─────────────┴───────┴─────────┴───────┘   └───────────────────┘
 **/
#ifndef __SYSTEM_CORE_H
#define __SYSTEM_CORE_H

/* ↓↓↓ Modify Configuration Area ↓↓↓ */
#include <stdint.h>      // device header file
#define TASK_MAX_NUM  20 // value ≥ 1
#define EVENT_MAX_NUM 10 // value ≥ 0
/* ↑↑↑ Modify Configuration Area ↑↑↑ */

#define SYS_PRINT_MSG(x) "[MinSys] " x
#if TASK_MAX_NUM <= 0
#error "TASK_MAX_NUM must be a positive integer (>=1)!"
#elif TASK_MAX_NUM > 65535
#error "TASK_MAX_NUM is too large (>65535)!"
#endif
#if EVENT_MAX_NUM < 0
#error "EVENT_MAX_NUM must be a nonnegative integer (>=0)!"
#elif EVENT_MAX_NUM == 0
#pragma message(SYS_PRINT_MSG("Disable Function: message task."))
#else
#define ENABLE_EVENT_TASK
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef void (*TaskMainFunc)(u32, u32); // task main function
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

/* System Function  */
void System_Init(void);
void System_Loop(void);
void System_EndLoop(void);
void System_RegisterIdleTask(TaskMainFunc func); // void (currIdleTick, lastIdleTick)

/* Task Creation Function */
Task *System_AddNewLoopTask(TaskMainFunc func, u32 interval);
Task *System_AddNewTempTask(TaskMainFunc func, u32 interval);
#ifdef ENABLE_EVENT_TASK
Task *System_AddNewEventTask(TaskMainFunc func, Event *event, u32 signal);
#endif

/* Global Task Operation Function */
bool System_SuspendTask(Task *task, u32 info);
bool System_ResumeTask(Task *task, u32 info, bool instance);
bool System_KillTask(Task *task);

#ifdef ENABLE_EVENT_TASK
/* Event Task Operation Function  */
Event *System_CreateEvent(void);
bool System_DeleteEvent(Event *event);
bool System_SetEvent(Event *event, u32 signal, u32 value);
u32 System_GetEventSignal(Event *event);
#endif

/* Current Task Operation Function */
bool Task_Delay(u16 ticks, u32 info);
bool Task_Suspend(u32 info);
#ifdef ENABLE_EVENT_TASK
bool Task_ListenSingal(u32 newSignal);
#endif
void Task_Close(void);

#ifdef __cplusplus
}
#endif

#endif