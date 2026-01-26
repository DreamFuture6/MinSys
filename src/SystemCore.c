#include "SystemCore.h"

#if TASK_MAX_NUM > 255
typedef u16 TaskIndex;
#else
typedef u8 TaskIndex;
#endif

#ifdef ENABLE_EVENT_TASK
#if EVENT_MAX_NUM > 255
typedef u16 EvtIndex;
#else
typedef u8 EvtIndex;
#endif

#define __EndOfEvtList ((EvtIndex) - 1)

typedef struct Event {
    bool enable;
    u32 signal;
    u32 value;
    TaskIndex subList;
} Event;

static Event eventList[EVENT_MAX_NUM];
static EvtIndex eventQueue[EVENT_MAX_NUM];
#endif

typedef enum TaskType {
    TASKTYPE_CIRCULATE,
    TASKTYPE_DISPOSABLE,
    TASKTYPE_EVENT,
} TaskType;

typedef union TaskInfo {
    struct {
        u32 interval;
        u32 nextRunTime;
        u32 count;
    } timebased;
#ifdef ENABLE_EVENT_TASK
    struct {
        bool suspend;
        Event *event;
        u32 signal;
    } eventbased;
#endif
} TaskInfo;

struct Task {
    TaskIndex curr;
    TaskType type;
    TaskMainFunc func;
    TaskInfo info;
    TaskIndex next;
    u32 paramInfo;
};

static bool looping;
static u16 taskFlag; // [0~7]:delay [8]:close [9]:suspend
static TaskMainFunc idleTask;

static TaskIndex currTimeTaskIndex, currExecTaskIndex;
static Task taskList[TASK_MAX_NUM];

#define __EndOfTaskList   ((TaskIndex) - 1)
#define FLAG_DELAY_MASK   ((1U << 8) - 1)
#define FLAG_CLOSE_MASK   (1U << 8)
#define FLAG_SUSPEND_MASK (1U << 9)

static inline bool __IsTaskParamInvalid(Task *task)
{
    if (task == NULL) {
        return true;
    }
    static Task *end = taskList + TASK_MAX_NUM - 1;
    return task < taskList || task > end || taskList[task->curr].func == NULL;
}

static inline void __InitTaskNode(TaskIndex i, TaskType type, TaskMainFunc func)
{
    taskList[i].type      = type;
    taskList[i].func      = func;
    taskList[i].info      = (TaskInfo){0};
    taskList[i].next      = __EndOfTaskList;
    taskList[i].paramInfo = 0;
}

static inline void __LinkTimebasedTaskNode(Task *task)
{
    TaskIndex prev = __EndOfTaskList, curr = currTimeTaskIndex;
    while (curr != __EndOfTaskList && task->info.timebased.nextRunTime >= taskList[curr].info.timebased.nextRunTime) {
        prev = curr;
        curr = taskList[curr].next;
    }
    if (prev == __EndOfTaskList) {
        task->next        = currTimeTaskIndex;
        currTimeTaskIndex = task->curr;
    } else {
        taskList[prev].next = task->curr;
        task->next          = curr;
    }
}

static inline bool __SetNextNodeOfPrevTaskNode(Task *task, TaskIndex startNode)
{
    TaskIndex prev = __EndOfTaskList, curr = startNode;
    while (curr != task->curr) {
        prev = curr;
        curr = taskList[curr].next;
        if (curr == __EndOfTaskList) {
            return true;
        }
    }
    taskList[prev].next = task->next;
    return false;
}

static inline void __ClearTaskNode(Task *task)
{
    task->next      = __EndOfTaskList;
    task->func      = NULL;
    task->info      = (TaskInfo){0};
    task->paramInfo = 0;
}

#ifdef ENABLE_EVENT_TASK
static inline bool __IsEventParamInvalid(Event *event)
{
    if (event == NULL) {
        return true;
    }
    const Event *end = eventList + EVENT_MAX_NUM - 1;
    return event < eventList || event > end || event->enable == false;
}

static inline void __DeleteEventTask(Task *task)
{
    __SetNextNodeOfPrevTaskNode(task, task->info.eventbased.event->subList);
    __ClearTaskNode(task);
}
#endif

static inline void __ResetTaskExecuteEnv(void)
{
    taskFlag = 0x0000;
}

void System_Init(void)
{
    looping           = false;
    currTimeTaskIndex = __EndOfTaskList;
    currExecTaskIndex = __EndOfTaskList;
    for (TaskIndex i = 0; i < TASK_MAX_NUM; ++i) {
        taskList[i].curr = i;
        taskList[i].next = __EndOfTaskList;
    }
    idleTask = NULL;
#ifdef ENABLE_EVENT_TASK
    eventQueue[0] = __EndOfEvtList;
#endif
}

void System_Loop(void)
{
    if (looping == true) {
        return;
    }
    looping          = true;
    u32 lastIdleTick = System_GetCurrTick();
    register Task *tempTask;
#ifdef ENABLE_EVENT_TASK
    register Event *tempEvent;
#endif
    while (looping) {
#ifdef ENABLE_EVENT_TASK
        for (EvtIndex i = 0; i < EVENT_MAX_NUM && eventQueue[i] != __EndOfEvtList; ++i) {
            tempEvent         = eventList + eventQueue[i];
            currExecTaskIndex = tempEvent->subList;
            while (currExecTaskIndex != __EndOfTaskList) {
                tempTask = taskList + currExecTaskIndex;
                if (tempTask->info.eventbased.suspend == false && tempTask->info.eventbased.signal == tempEvent->signal) {
                    __ResetTaskExecuteEnv();
                    tempTask->func(tempEvent->signal, tempEvent->value);
                    if (taskFlag) {
                        if (taskFlag & FLAG_CLOSE_MASK) {
                            currExecTaskIndex = tempTask->next;
                            __DeleteEventTask(tempTask);
                            continue;
                        } else if (taskFlag & FLAG_SUSPEND_MASK) {
                            tempTask->info.eventbased.suspend = true;
                        }
                    }
                }
                currExecTaskIndex = tempTask->next;
            }
            tempEvent->signal = 0;
        }
        eventQueue[0] = __EndOfEvtList;
#endif
        if (currTimeTaskIndex != __EndOfTaskList) {
            if (System_GetCurrTick() >= taskList[currTimeTaskIndex].info.timebased.nextRunTime) {
                currExecTaskIndex = currTimeTaskIndex;
                tempTask          = taskList + currExecTaskIndex;
                __ResetTaskExecuteEnv();
                switch (tempTask->type) {
                case TASKTYPE_CIRCULATE:
                    tempTask->func(tempTask->info.timebased.count, tempTask->paramInfo);
                    currTimeTaskIndex = tempTask->next;
                    if (taskFlag) {
                        if (taskFlag & FLAG_CLOSE_MASK) {
                            __ClearTaskNode(taskList + currExecTaskIndex);
                            break;
                        } else if (taskFlag & FLAG_SUSPEND_MASK) {
                            break;
                        } else if (taskFlag & FLAG_DELAY_MASK) {
                            tempTask->info.timebased.nextRunTime += taskFlag & FLAG_DELAY_MASK;
                        }
                    } else {
                        tempTask->info.timebased.count++;
                        tempTask->info.timebased.nextRunTime += tempTask->info.timebased.interval;
                        tempTask->paramInfo = 0;
                    }
                    __LinkTimebasedTaskNode(taskList + currExecTaskIndex);
                    break;
                case TASKTYPE_DISPOSABLE:
                    tempTask->func(0, tempTask->paramInfo);
                    currTimeTaskIndex = tempTask->next;
                    if (taskFlag & FLAG_DELAY_MASK) {
                        tempTask->info.timebased.nextRunTime += taskFlag & FLAG_DELAY_MASK;
                        __LinkTimebasedTaskNode(taskList + currExecTaskIndex);
                    } else {
                        __ClearTaskNode(taskList + currExecTaskIndex);
                    }
                    break;
                }
            } else if (idleTask) {
                u32 currIdleTick = System_GetCurrTick();
                idleTask(currIdleTick, lastIdleTick);
                lastIdleTick = currIdleTick;
            }
        }
    }
}

void System_RegisterIdleTask(TaskMainFunc func)
{
    idleTask = func;
}

void System_EndLoop(void)
{
    looping = false;
}

Task *System_AddNewLoopTask(TaskMainFunc func, u32 interval)
{
    for (TaskIndex i = 0; i < TASK_MAX_NUM; ++i) {
        if (taskList[i].func == NULL) {
            __InitTaskNode(i, TASKTYPE_CIRCULATE, func);
            taskList[i].info.timebased.nextRunTime = System_GetCurrTick() + interval;
            taskList[i].info.timebased.interval    = interval;
            __LinkTimebasedTaskNode(taskList + i);
            return taskList + i;
        }
    }
    return NULL;
}

Task *System_AddNewTempTask(TaskMainFunc func, u32 interval)
{
    for (TaskIndex i = 0; i < TASK_MAX_NUM; ++i) {
        if (taskList[i].func == NULL) {
            __InitTaskNode(i, TASKTYPE_DISPOSABLE, func);
            taskList[i].info.timebased.nextRunTime = System_GetCurrTick() + interval;
            __LinkTimebasedTaskNode(taskList + i);
            return taskList + i;
        }
    }
    return NULL;
}

#ifdef ENABLE_EVENT_TASK
Task *System_AddNewEventTask(TaskMainFunc func, Event *event, u32 signal)
{
    if (signal == 0 || __IsEventParamInvalid(event)) {
        return NULL;
    }
    for (TaskIndex i = 0; i < TASK_MAX_NUM; ++i) {
        if (taskList[i].func == NULL) {
            __InitTaskNode(i, TASKTYPE_EVENT, func);
            taskList[i].info.eventbased.event  = event;
            taskList[i].info.eventbased.signal = signal;

            TaskIndex j = event->subList;
            if (j == __EndOfTaskList) {
                event->subList = i;
            } else {
                while (taskList[j].next != __EndOfTaskList) {
                    j = taskList[j].next;
                }
                taskList[j].next = i;
            }
            return taskList + i;
        }
    }
    return NULL;
}
#endif

bool System_SuspendTask(Task *task, u32 info)
{
    if (__IsTaskParamInvalid(task) || task->type == TASKTYPE_DISPOSABLE || info & 0x80000000) {
        return false;
    }
    if (currExecTaskIndex == task->curr) {
        taskFlag |= FLAG_SUSPEND_MASK;
        return true;
    }
#ifdef ENABLE_EVENT_TASK
    if (task->type == TASKTYPE_EVENT) {
        task->info.eventbased.suspend = true;
        return true;
    }
#endif
    if (__SetNextNodeOfPrevTaskNode(task, currTimeTaskIndex)) {
        return false;
    }
    task->next      = __EndOfTaskList;
    task->paramInfo = info | 0x80000000;
    return true;
}

bool System_ResumeTask(Task *task, u32 info, bool instance)
{
    if (__IsTaskParamInvalid(task) || task->type == TASKTYPE_DISPOSABLE || info & 0x80000000) {
        return false;
    }
#ifdef ENABLE_EVENT_TASK
    if (task->type == TASKTYPE_EVENT) {
        task->info.eventbased.suspend = false;
        return true;
    }
#endif
    task->paramInfo                  = info | 0x80000000;
    task->info.timebased.nextRunTime = System_GetCurrTick() + (instance ? 0 : task->info.timebased.interval);
    __LinkTimebasedTaskNode(task);
    return true;
}

bool System_KillTask(Task *task)
{
    if (__IsTaskParamInvalid(task)) {
        return false;
    }
    if (currExecTaskIndex == task->curr) {
        taskFlag |= FLAG_CLOSE_MASK;
        return true;
    }
    switch (task->type) {
    case TASKTYPE_CIRCULATE:
    case TASKTYPE_DISPOSABLE: {
        if (currTimeTaskIndex == __EndOfTaskList) {
            return false;
        }
        if (__SetNextNodeOfPrevTaskNode(task, currTimeTaskIndex)) {
            return false;
        }
        __ClearTaskNode(task);
        return true;
    }
#ifdef ENABLE_EVENT_TASK
    case TASKTYPE_EVENT:
        if (task->info.eventbased.event->subList == __EndOfTaskList) {
            return false;
        }
        __DeleteEventTask(task);
        return true;
#endif
    default:
        return false;
    }
}

#ifdef ENABLE_EVENT_TASK
Event *System_CreateEvent(void)
{
    for (EvtIndex i = 0; i < EVENT_MAX_NUM; ++i) {
        if (eventList[i].enable == false) {
            eventList[i].enable  = true;
            eventList[i].signal  = 0;
            eventList[i].value   = 0;
            eventList[i].subList = __EndOfTaskList;
            return eventList + i;
        }
    }
    return NULL;
}

bool System_DeleteEvent(Event *event)
{
    if (__IsEventParamInvalid(event) || event->subList != __EndOfTaskList) {
        return false;
    }
    event->enable = false;
    return true;
}

bool System_SetEvent(Event *event, u32 signal, u32 value)
{
    if (__IsEventParamInvalid(event) || signal == 0 || event->signal == signal) {
        return false;
    }
    event->signal = signal;
    event->value  = value;
    for (EvtIndex i = 0; i < EVENT_MAX_NUM; ++i) {
        if (eventList + eventQueue[i] == event) {
            return true;
        }
        if (eventQueue[i] == __EndOfEvtList) {
            eventQueue[i] = (EvtIndex)(event - eventList);
            if (i + 1 < EVENT_MAX_NUM) {
                eventQueue[i + 1] = __EndOfEvtList;
            }
            return true;
        }
    }
    return false;
}

u32 System_GetEventSignal(Event *event)
{
    return event->signal;
}
#endif

bool Task_Delay(u16 ticks, u32 info)
{
    if (currExecTaskIndex == __EndOfTaskList ||
#ifdef ENABLE_EVENT_TASK
        taskList[currExecTaskIndex].type == TASKTYPE_EVENT ||
#endif
        info & 0x80000000) {
        return false;
    }
    taskFlag |= (ticks & FLAG_DELAY_MASK);
    taskList[currExecTaskIndex].paramInfo = info;
    return true;
}

bool Task_Suspend(u32 info)
{
    if (currExecTaskIndex == __EndOfTaskList || taskList[currExecTaskIndex].type == TASKTYPE_DISPOSABLE || info & 0x80000000) {
        return false;
    }
    taskFlag |= FLAG_SUSPEND_MASK;
    taskList[currExecTaskIndex].paramInfo = info | 0x80000000;
    return true;
}

#ifdef ENABLE_EVENT_TASK
bool Task_ListenSingal(u32 newSignal)
{
    if (currExecTaskIndex == __EndOfTaskList || taskList[currExecTaskIndex].type != TASKTYPE_EVENT || newSignal == 0) {
        return false;
    }
    taskList[currExecTaskIndex].info.eventbased.signal = newSignal;
    return true;
}
#endif

void Task_Close(void)
{
    taskFlag |= FLAG_CLOSE_MASK;
}