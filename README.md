# MinSys - Lightweight Soft Real-Time Task Scheduling Kernel

MinSys is a lightweight **soft real-time task scheduling kernel** designed for embedded systems.
It focuses on **low resource consumption**, **deterministic behavior**, and **simple APIs**, supporting both **time-driven tasks** and **event-driven tasks**.

MinSys is suitable for bare-metal or RTOS-less environments where a full RTOS would be too heavy, but structured task scheduling is still required.

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

Kernel configuration is defined in **`SystemConfig.h`**.
These macros control resource limits and optional features at **compile time**.

| Configuration Item     | Description                                                                                                                             |
| ---------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| `TASK_MAX_NUM`       | **Mandatory.** Maximum number of tasks supported by the kernel (≥1 and ≤65535). Determines the size of the internal task table. |
| `EVENT_MAX_NUM`      | Maximum number of event objects. Setting this to `0` disables all event-related features at compile time.                             |
| `IDLE_HOOK_FUNCITON` | Optional macro. When defined, allows registering an idle hook function executed when no task is runnable.                               |
| `AUTO_SLEEP`         | Optional macro. When enabled and event features are active, the kernel calls `System_Sleep()` during idle time to save power.         |

> **Note**
> When `EVENT_MAX_NUM > 0`, the kernel internally reserves a system management task.
> Therefore, `TASK_MAX_NUM` must be **at least 2**.

---

## Global Dependencies

The following functions **must be implemented by the user** to adapt MinSys to the target platform:

| Function                         | Description                                                                                                           |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| `u32 System_GetCurrTick(void)` | Returns the current system tick used for time comparison in scheduling. Must be monotonically increasing.             |
| `void System_Sleep(void)`      | Required**only when `AUTO_SLEEP` is enabled**. Called by the kernel during idle periods in event-driven mode. |

---

## Core Interfaces

### Initialization & Runtime Control

| Function                      | Description                                                                   | Calling Constraints                                               |
| ----------------------------- | ----------------------------------------------------------------------------- | ----------------------------------------------------------------- |
| `void System_Init(void)`    | Initializes kernel data structures (task table, event queue, internal flags). | Must be called**once** before any task creation.            |
| `void System_Loop(void)`    | Enters the main scheduling loop and starts task execution.                    | Can be called only once; subsequent calls are ignored internally. |
| `void System_EndLoop(void)` | Requests termination of `System_Loop()`.                                    | Can be called from other tasks or interrupt contexts.             |

### Idle Hook

Available only when `IDLE_HOOK_FUNCITON` is enabled.

| Function                                            | Description                                                                                                             |
| --------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `void System_RegisterIdleTask(TaskMainFunc func)` | Registers a callback function executed during idle time. Parameters passed are `(current_idle_tick, last_idle_tick)`. |

### Task Creation

| Function                                                                      | Description                                                     | Parameters / Return Value                                                                                                          |
| ----------------------------------------------------------------------------- | --------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| `Task* System_AddNewLoopTask(TaskMainFunc func, u32 interval)`              | Creates a periodic (loop) task.                                 | `func`: task function `<br>interval`: execution period (ticks)`<br>`Returns task handle or `NULL`.                         |
| `Task* System_AddNewTempTask(TaskMainFunc func, u32 interval)`              | Creates a one-time task.                                        | Same parameters and return value as above.                                                                                         |
| `Task* System_AddNewEventTask(TaskMainFunc func, Event* event, u32 signal)` | Creates an event-driven task (only when `EVENT_MAX_NUM > 0`). | `func`: task function `<br>event`: event object `<br>signal`: non-zero signal value `<br>`Returns task handle or `NULL`. |

### Global Task Operations

Applicable to **any valid task handle**.

| Function                                                        | Description                                                           | Parameters / Return Value                                                                       |
| --------------------------------------------------------------- | --------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `bool System_SuspendTask(Task* task, u32 info)`               | Suspends a periodic or event task (not supported for one-time tasks). | `task`: task handle `<br>info`: suspend info (MSB must be 1)`<br>`Returns success status. |
| `bool System_ResumeTask(Task* task, u32 info, bool instance)` | Resumes a suspended task.                                             | `info`: resume info (MSB must be 0)`<br>instance`: `true` for immediate execution.        |
| `bool System_KillTask(Task* task)`                            | Deletes a task and releases its slot.                                 | Returns success status.                                                                         |

### Event-Related Interfaces

Available only when `EVENT_MAX_NUM > 0`.

| Function                                                      | Description                                       | Parameters / Return Value                         |
| ------------------------------------------------------------- | ------------------------------------------------- | ------------------------------------------------- |
| `Event* System_CreateEvent(void)`                           | Creates an event object.                          | Returns event handle or `NULL`.                 |
| `bool System_DeleteEvent(Event* event)`                     | Deletes an event object.                          | Returns `true` only if no tasks are subscribed. |
| `bool System_SetEvent(Event* event, u32 signal, u32 value)` | Triggers an event with signal and attached value. | Returns success status.                           |
| `u32 System_GetEventSignal(Event* event)`                   | Reads the current signal value of the event.      | Read-only access.                                 |

### Current Task Operations

Valid **only when called inside a task function**.

| Function                                  | Description                                             | Parameters / Return Value                                          |
| ----------------------------------------- | ------------------------------------------------------- | ------------------------------------------------------------------ |
| `bool Task_Delay(u16 ticks, u32 info)`  | Delays execution of the current time-driven task.       | `ticks`: delay duration `<br>info`: task info (MSB must be 0). |
| `bool Task_Suspend(u32 info)`           | Suspends the current periodic task.                     | Not supported for event or one-time tasks.                         |
| `bool Task_ListenSingal(u32 newSignal)` | Changes the signal monitored by the current event task. | `newSignal` must be non-zero.                                    |
| `void Task_Close(void)`                 | Requests deletion of the current task after it returns. | No return value.                                                   |

---

## Task-Supported Operations Table

|  TASK TYPE  | YIELD | DELAY | SUSPEND | ASYNC | CLOSE |   Main Func Param   |
| :---------: | :---: | :---: | :-----: | :---: | :---: | :-----------------: |
| circle task |  √  |  √  |   √   |  √  |  √  | `(count, state)` |
| single task |  √  |  √  |   ×   |  ×  |  √  |   `(0, state)`   |
| events task |  ×  |  √  |   √   |  √  |  √  | `(value, signal)` |

> **Note**
> For event tasks, when a delay expires, parameters passed to the task function are `(0, 0)`.

---

## Usage Example

```c
#include <time.h>
#include "SystemCore.h"

const Event* event;

u32 System_GetCurrTick(void) {
    return clock();
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
    System_AddNewLoopTask(LoopTask, 100);          // Executes every 100 ticks
    System_AddNewTempTask(OnceTask, 50);           // Executes once after 50 ticks
    System_AddNewEventTask(EventTask, event, 10);  // Executes when the signal of 'event' turns to 10
  
    // 4. Enter the main scheduling loop
    System_Loop();
  
    return 0;
}
```

---

## Notes

* Task and event handles must only be used if returned successfully by creation APIs.
* The highest bit (`0x80000000`) of the `info` parameter is reserved by the kernel.
* One-time tasks do not support suspend/resume operations.
* Task functions should be short and non-blocking.
* `System_GetCurrTick()` must return a monotonically increasing value.

---

## License

MinSys is released under the GPL-3.0 License. See [LICENSE](LICENSE) for the full text.
