# MinSys - Lightweight Soft Real-Time Task Scheduling Kernel

MinSys is a lightweight soft real-time task scheduling kernel for embedded systems.
It focuses on low resource consumption, deterministic behavior, and simple APIs, supporting both time-driven tasks and event-driven tasks.

---

## Table of Contents

- [Configuration](#configuration)
- [Global Dependencies](#global-dependencies)
- [Core Interfaces](#core-interfaces)
  - [Initialization &amp; Runtime Control](#initialization--runtime-control)
  - [Idle Hook](#idle-hook)
  - [Task Creation](#task-creation)
  - [Global Task Operations](#global-task-operations)
  - [Event-Related Interfaces](#event-related-interfaces)
  - [Current Task Operations](#current-task-operations)
- [Task-Supported Operations Table](#task-supported-operations-table)
- [Usage Example](#usage-example)
- [Notes](#notes)
- [License](#license)

---

## Configuration

Kernel configuration is defined in `inc/SystemConfig.h`.
These macros control resource limits and optional features at compile time.

| Configuration Item     | Description                                                                                                                                               |
| ---------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `TASK_MAX_NUM`       | Maximum number of tasks. Must be `>= 1`. If `EVENT_MAX_NUM > 0`, must be `>= 2` because the system event handler task uses one slot. Max `65535`. |
| `EVENT_MAX_NUM`      | Maximum number of event objects. Setting this to `0` disables event-related features at compile time.                                                   |
| `IDLE_HOOK_FUNCTION` | Optional macro. Enables registering an idle hook executed when no task is runnable.                                                                       |
| `AUTO_SLEEP`         | Optional macro. Only valid when event features are enabled. Calls `System_Sleep()` during idle periods.                                                 |

---

## Global Dependencies

The following functions must be implemented by the user to adapt MinSys to the target platform:

| Function                         | Description                                                                                                  |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `u32 System_GetCurrTick(void)` | Returns the current system tick used for time comparison in scheduling. Must be monotonically increasing.    |
| `void System_Sleep(void)`      | Required only when `AUTO_SLEEP` is enabled. Called by the kernel during idle periods in event-driven mode. |

---

## Core Interfaces

### Initialization & Runtime Control

| Function                      | Description                                                                   | Calling Constraints                                    |
| ----------------------------- | ----------------------------------------------------------------------------- | ------------------------------------------------------ |
| `void System_Init(void)`    | Initializes kernel data structures (task table, event queue, internal flags). | Must be called once before any task creation.          |
| `void System_Loop(void)`    | Enters the main scheduling loop and starts task execution.                    | Can be called only once; subsequent calls are ignored. |
| `void System_EndLoop(void)` | Requests termination of `System_Loop()`.                                    | Can be called from other tasks or interrupt contexts.  |

### Idle Hook

Available only when `IDLE_HOOK_FUNCTION` is enabled.

| Function                                            | Description                                                                                             |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| `void System_RegisterIdleTask(TaskMainFunc func)` | Registers a callback executed during idle time. Parameters are `(current_idle_tick, last_idle_tick)`. |

### Task Creation

| Function                                                                                | Description                                                     | Parameters / Return Value                                                                                                                                         |
| --------------------------------------------------------------------------------------- | --------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `TaskHandle *System_AddNewTimeTask(TaskMainFunc func, u32 interval, bool disposable)` | Creates a time-driven task.                                     | `func`: task function. `interval`: execution period in ticks. `disposable`: `true` for one-shot, `false` for periodic. Returns task handle or `NULL`. |
| `TaskHandle *System_AddNewEventTask(TaskMainFunc func, EvtHandle *event, u16 signal)` | Creates an event-driven task (only when `EVENT_MAX_NUM > 0`). | `func`: task function. `event`: event object. `signal`: non-zero signal value. Returns task handle or `NULL`.                                             |

### Global Task Operations

Applicable to any valid task handle.

| Function                                                                   | Description                                                           | Parameters / Return Value                                                                                                                              |
| -------------------------------------------------------------------------- | --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `bool System_SuspendTask(TaskHandle *task, u16 nextState)`               | Suspends a periodic or event task (not supported for one-shot tasks). | `nextState`: state stored for the next run. Returns success status.                                                                                  |
| `bool System_ResumeTask(TaskHandle *task, u16 execState, bool instance)` | Resumes a suspended task.                                             | `execState`: state passed on next run. `instance`: `true` for immediate execution, `false` for normal interval timing. Returns success status. |
| `bool System_KillTask(TaskHandle *task)`                                 | Deletes a task and releases its slot.                                 | Returns success status.                                                                                                                                |

### Event-Related Interfaces

Available only when `EVENT_MAX_NUM > 0`.

| Function                                                          | Description                                       | Parameters / Return Value                         |
| ----------------------------------------------------------------- | ------------------------------------------------- | ------------------------------------------------- |
| `EvtHandle *System_CreateEvent(void)`                           | Creates an event object.                          | Returns event handle or `NULL`.                 |
| `bool System_DeleteEvent(EvtHandle *event)`                     | Deletes an event object.                          | Returns `true` only if no tasks are subscribed. |
| `bool System_SetEvent(EvtHandle *event, u16 signal, u32 value)` | Triggers an event with signal and attached value. | Returns success status.                           |
| `u16 System_GetEventSignal(EvtHandle *event)`                   | Reads the current signal value of the event.      | Read-only access.                                 |

### Current Task Operations

Valid only when called inside a task function.

| Function                                      | Description                                                           | Parameters / Return Value                                                                |
| --------------------------------------------- | --------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| `bool Task_Yield(u16 nextState)`            | Yields execution of the current time task. Not valid for event tasks. | `nextState`: state passed on next run.                                                 |
| `bool Task_Delay(u16 ticks, u16 nextState)` | Delays execution of the current task.                                 | `ticks`: delay duration, low 8 bits are used. `nextState`: state passed on next run. |
| `bool Task_Suspend(u16 nextState)`          | Suspends the current task. Not supported for one-shot tasks.          | `nextState`: state passed on next run.                                                 |
| `bool Task_ListenSignal(u16 newSignal)`     | Changes the signal monitored by the current event task.               | `newSignal` must be non-zero.                                                          |
| `bool Task_Close(void)`                     | Requests deletion of the current task after it returns.               | Returns success status.                                                                  |

---

## Task-Supported Operations Table

|  Task Type  | YIELD | DELAY | SUSPEND | CLOSE |   Main Func Param   |
| :---------: | :---: | :---: | :-----: | :---: | :-----------------: |
| circle task |  √  |  √  |   √   |  √  | `(count, state)` |
| single task |  √  |  √  |   ×   |  √  |   `(0, state)`   |
| events task |  ×  |  √  |   √   |  √  | `(value, signal)` |

Note: For event tasks, when a delay expires, parameters passed to the task function are `(0, 0)`.

---

## Usage Example

```c
#include <time.h>
#include "SystemCore.h"

EvtHandle *event;

u32 System_GetCurrTick(void) {
    return (u32)clock();
}

void LoopTask(u32 count, u16 state) {
    // Do something...
    System_SetEvent(event, 10, 123);
    if (count > 10) {
        Task_Close();
    }
    if (state == 0) {
        Task_Delay(50, 1);
    }
}

void OnceTask(u32 unused, u16 state) {
    // Do something...
}

void EventTask(u32 value, u16 signal) {
    // Do something...
}

int main(void) {
    // 1. Initialize the kernel
    System_Init();

    // 2. Create events
    event = System_CreateEvent();

    // 3. Create tasks
    System_AddNewTimeTask(LoopTask, 100, false);
    System_AddNewTimeTask(OnceTask, 50, true);
    System_AddNewEventTask(EventTask, event, 10);

    // 4. Enter the main scheduling loop
    System_Loop();

    return 0;
}
```

---

## Notes

- Task and event handles must only be used if returned successfully by creation APIs.
- `System_GetCurrTick()` must return a monotonically increasing value.
- `Task_Delay` uses only the low 8 bits of `ticks` (`0` to `255`).
- `System_SetEvent` ignores a signal that matches the current pending signal for the same event.
- Task functions should be short and non-blocking.

---

## License

MinSys is released under the GPL-3.0 License. See `LICENSE` for the full text.
