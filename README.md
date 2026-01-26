# MinSys - Lightweight Soft Real-Time Task Scheduling Kernel

MinSys is a lightweight soft real-time task scheduling kernel designed for embedded systems. It features low resource consumption and supports scheduling of time-driven and event-driven tasks.

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
- [Usage Example](#usage-example)
- [Notes](#notes)
- [License](#license)

## Configuration

Configuration items are defined in `SystemConfig.h` to customize kernel functions and resource limits:

| Configuration Item     | Description                                                                                                                                    |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `TASK_MAX_NUM`       | Mandatory configuration. Maximum number of tasks (≥1 and ≤65535), determining the size of the task handle array.                             |
| `EVENT_MAX_NUM`      | Number of event objects. 0 disables event features (takes effect at compile time); values >0 enable event-related interfaces.                  |
| `IDLE_HOOK_FUNCITON` | Optional comment macro. Defining it allows registering an idle task to be called during idle time.                                             |
| `AUTO_SLEEP`         | Optional configuration. When there are no time-driven tasks and event features are enabled, the kernel calls `System_Sleep()` to save power. |

## Global Dependencies

The following functions must be implemented by the user/platform:

| Function                         | Description                                                                                               |
| -------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `u32 System_GetCurrTick(void)` | Returns the current time unit (tick) for time comparison in task scheduling.                              |
| `void System_Sleep(void)`      | Required only when `AUTO_SLEEP` is enabled. Called by the kernel during idle time in event-driven mode. |

## Core Interfaces

### Initialization & Runtime Control

| Function                      | Description                                                                                  | Calling Constraints                                                                                                              |
| ----------------------------- | -------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| `void System_Init(void)`    | Initializes kernel data structures (task table, event queue, etc.) and resets runtime flags. | Must be called before any task registration, only once at program startup.                                                       |
| `void System_Loop(void)`    | Enters the kernel main loop to schedule and execute ready tasks.                             | Only allowed to enter once (internal looping flag ignores repeated calls); processes event queues first, then time-driven tasks. |
| `void System_EndLoop(void)` | Sets the exit flag to terminate the `System_Loop` main loop.                               | Must be called in other tasks/interrupts or the main thread.                                                                     |

### Idle Hook

Available only when `IDLE_HOOK_FUNCITON` is enabled:

| Function                                            | Description                                                                                             |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| `void System_RegisterIdleTask(TaskMainFunc func)` | Registers a function to be called during idle time. Parameters are (current idle tick, last idle tick). |

### Task Creation

| Function                                                                      | Description                                                                              | Parameters/Return Value                                                                                                                                                                                   |
| ----------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Task *System_AddNewLoopTask(TaskMainFunc func, u32 interval)`              | Creates and registers a periodic task.                                                   | Parameters:`<br>`- func: Task main function `<br>`- interval: Execution period (in ticks)`<br>`Return: Task handle on success, NULL on failure.                                                     |
| `Task *System_AddNewTempTask(TaskMainFunc func, u32 interval)`              | Creates and registers a one-time task.                                                   | Same parameters/return value as above.                                                                                                                                                                    |
| `Task *System_AddNewEventTask(TaskMainFunc func, Event *event, u32 signal)` | Registers an event task for a specified event (available only when `EVENT_MAX_NUM>0`). | Parameters:`<br>`- func: Task main function `<br>`- event: Event object created by `System_CreateEvent<br>`- signal: Non-zero signal value `<br>`Return: Task handle on success, NULL on failure. |

### Global Task Operations

Applicable to any valid task handle:

| Function                                                        | Description                                                                    | Parameters/Return Value                                                                                                                                                                                                     |
| --------------------------------------------------------------- | ------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `bool System_SuspendTask(Task *task, u32 info)`               | Suspends the specified periodic/event task (not supported for one-time tasks). | Parameters:`<br>`- task: Task handle `<br>`- info: Suspend info (highest bit must be 1)`<br>`Return: True on success, false on failure.                                                                               |
| `bool System_ResumeTask(Task *task, u32 info, bool instance)` | Resumes a suspended periodic/event task (not supported for one-time tasks).    | Parameters:`<br>`- task: Task handle `<br>`- info: Resume info (highest bit must be 0)`<br>`- instance: True for immediate execution, false for periodic execution `<br>`Return: True on success, false on failure. |
| `bool System_KillTask(Task *task)`                            | Deletes/closes a task and releases the task slot.                              | Parameter: task - Task handle `<br>`Return: True on success, false on failure.                                                                                                                                            |

### Event-Related Interfaces

Available only when `EVENT_MAX_NUM>0`:

| Function                                                      | Description                                                                                 | Parameters/Return Value                                                                                                                                                   |
| ------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Event *System_CreateEvent(void)`                           | Creates an event object (subscribable by tasks).                                            | Return: Event handle on success, NULL on failure.                                                                                                                         |
| `bool System_DeleteEvent(Event *event)`                     | Deletes an event object.                                                                    | Parameter: event - Event handle `<br>`Return: True only if there are no subscribed tasks, false otherwise.                                                              |
| `bool System_SetEvent(Event *event, u32 signal, u32 value)` | Triggers an event, sets signal and attached value, and pushes the event to the event queue. | Parameters:`<br>`- event: Event handle `<br>`- signal: Non-zero signal value `<br>`- value: Event attached value `<br>`Return: True on success, false on failure. |
| `u32 System_GetEventSignal(Event *event)`                   | Reads the current signal value of the event (read-only).                                    | Parameter: event - Event handle `<br>`Return: Current signal value.                                                                                                     |

### Current Task Operations

Valid only when called inside `TaskMainFunc`:

| Function                                  | Description                                                                                                                        | Parameters/Return Value                                                                                                                                  |
| ----------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `bool Task_Delay(u16 ticks, u32 info)`  | Delays the current time-driven task (not supported for event tasks).                                                               | Parameters:`<br>`- ticks: Delay duration (in ticks)`<br>`- info: Task info (highest bit must be 0)`<br>`Return: True on success, false on failure. |
| `bool Task_Suspend(u32 info)`           | Suspends the current periodic task (not supported for event/one-time tasks).                                                       | Parameter: info - Suspend info `<br>`Return: True on success, false on failure.                                                                        |
| `bool Task_ListenSingal(u32 newSignal)` | Modifies the signal monitored by the current event task (available only for event tasks).                                          | Parameter: newSignal - Non-zero new signal value `<br>`Return: True on success, false on failure.                                                      |
| `void Task_Close(void)`                 | Requests to close/delete the current task. Sets the CLOSE flag, and the kernel cleans up the task slot after the function returns. | No parameters or return value.                                                                                                                           |

## Usage Example

```c
#include <time.h>
#include "SystemCore.h"

Event* event;

u32 System_GetCurrTick(void) {
    return clock();
}

void LoopTask(u32 count, u32 info) {
    // Do something...
    System_SetEvent(event, 10, 123);
    if (count > 10) {
        Task_Close();
    }
    if (info == 0) {
        Task_Delay(50, 1);
    }
}

void OnceTask(u32 unused, u32 info) {
    // Do something...
}

void EventTask(u32 signal, u32 value) {
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

## Notes

* Task handles (`Task*`) and event handles (`Event*`) can only use valid values returned by corresponding creation interfaces. Do not access them after deletion.
* `info` parameter convention: The highest bit (0x80000000) is reserved by the kernel to distinguish suspend/resume flags. Strictly follow interface requirements when passing values (some APIs require this bit to be 0).
* One-time tasks do not support suspend/resume operations (e.g., `System_SuspendTask`, `Task_Suspend`, `System_ResumeTask`).
* Task functions should be short and non-blocking. Long-term operations are recommended to be split into segments or scheduled again via `Task_Delay`.
* `System_GetCurrTick` must be implemented as a monotonically increasing timing logic to ensure scheduling accuracy.

## License
MinSys is released under the GPL-3.0 License. See [LICENSE](LICENSE) for the full text.
