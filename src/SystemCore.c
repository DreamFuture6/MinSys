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
    u32 value;
    u16 signal;
    bool enable;
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
        u16 signal;
        Event *event;
        u32 nextRunTime;
    } eventbased;
#endif
} TaskInfo;

struct Task {
    u16 execState;
    TaskIndex curr;
    TaskIndex next;
    TaskType type;
    TaskMainFunc func;
    TaskInfo info;
};

static bool looping;
static u16 taskFlag; // [0~7]:delay [8]:delay [9]:close [10]:suspend
#ifdef IDLE_HOOK_FUNCITON
static TaskMainFunc idleTask;
#endif

static TaskIndex currTimeTaskIndex, currExecTaskIndex;
static Task taskList[TASK_MAX_NUM];

#define __EndOfTaskList   ((TaskIndex) - 1)
#define DELAY_TIME_MASK   ((u16)((1U << 8) - 1))
#define FLAG_DELAY_MASK   ((u16)(1U << 8))
#define FLAG_CLOSE_MASK   ((u16)(1U << 9))
#define FLAG_SUSPEND_MASK ((u16)(1U << 10))
#define FLAG_YIELD_MASK   ((u16)(1U << 11))

static inline bool __IsTaskParamInvalid(Task *task)
{
    if (task == NULL) {
        return true;
    }
    static Task *end = taskList + TASK_MAX_NUM - 1;
    return task < taskList || task > end || taskList[task->curr].func == NULL;
}

static inline void __ClearTaskNode(Task *task)
{
    task->next      = __EndOfTaskList;
    task->func      = NULL;
    task->info      = (TaskInfo){0};
    task->execState = 0;
}

static inline void __InitTaskNode(Task *task, TaskType type, TaskMainFunc func)
{
    __ClearTaskNode(task);
    task->type = type;
    task->func = func;
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

static inline void __ResetTaskExecuteEnv(void)
{
    taskFlag = 0x0000;
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
    Event *e = task->info.eventbased.event;
    if (e->subList == task->curr) {
        e->subList = task->next;
    } else {
        __SetNextNodeOfPrevTaskNode(task, e->subList);
    }
    __ClearTaskNode(task);
}

static void __SystemEventHandlerTask(u32 count, u16 state)
{
    TaskIndex ti = __EndOfTaskList, ci = currExecTaskIndex;
    for (EvtIndex ei = (EvtIndex)state; ei < EVENT_MAX_NUM; ++ei) {
        if (eventList[ei].enable) {
            ti = eventList[ei].subList;
            while (ti != __EndOfTaskList) {
                if (taskList[ti].info.eventbased.nextRunTime && taskList[ti].info.eventbased.nextRunTime <= System_GetCurrTick()) {
                    currExecTaskIndex = ti;
                    __ResetTaskExecuteEnv();
                    taskList[ti].func(0, 0);
                    taskList[ti].info.eventbased.nextRunTime = 0;
                    if (taskFlag) {
                        if (taskFlag & FLAG_CLOSE_MASK) {
                            __DeleteEventTask(taskList + ti);
                        } else if (taskFlag & FLAG_SUSPEND_MASK) {
                            taskList[ti].info.eventbased.suspend = true;
                        } else if (taskFlag & FLAG_DELAY_MASK) {
                            taskList[ti].info.eventbased.nextRunTime = System_GetCurrTick() + (taskFlag & DELAY_TIME_MASK);
                        }
                    }
                    currExecTaskIndex = ci;
                    Task_Yield(ei);
                    return;
                }
                ti = taskList[ti].next;
            }
        }
    }
}
#endif

void System_Init(void)
{
    looping           = false;
    currTimeTaskIndex = __EndOfTaskList;
    currExecTaskIndex = __EndOfTaskList;
    for (TaskIndex i = 0; i < TASK_MAX_NUM; ++i) {
        taskList[i].curr = i;
        taskList[i].next = __EndOfTaskList;
    }
#ifdef IDLE_HOOK_FUNCITON
    idleTask = NULL;
#endif
#ifdef ENABLE_EVENT_TASK
    eventQueue[0] = __EndOfEvtList;
    System_AddNewLoopTask(__SystemEventHandlerTask, 1);
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
                if (tempTask->info.eventbased.suspend == false && tempTask->info.eventbased.nextRunTime == 0 && tempTask->info.eventbased.signal == tempEvent->signal) {
                    __ResetTaskExecuteEnv();
                    tempTask->func(tempEvent->value, tempEvent->signal);
                    if (taskFlag) {
                        if (taskFlag & FLAG_CLOSE_MASK) {
                            currExecTaskIndex = tempTask->next;
                            __DeleteEventTask(tempTask);
                            continue;
                        } else if (taskFlag & FLAG_SUSPEND_MASK) {
                            tempTask->info.eventbased.suspend = true;
                        } else if (taskFlag & FLAG_DELAY_MASK) {
                            tempTask->info.eventbased.nextRunTime = System_GetCurrTick() + (taskFlag & DELAY_TIME_MASK);
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
                    tempTask->func(tempTask->info.timebased.count, tempTask->execState);
                    currTimeTaskIndex = tempTask->next;
                    if (taskFlag) {
                        if (taskFlag & FLAG_CLOSE_MASK) {
                            __ClearTaskNode(taskList + currExecTaskIndex);
                            break;
                        } else if (taskFlag & FLAG_SUSPEND_MASK) {
                            break;
                        } else if (taskFlag & FLAG_DELAY_MASK) {
                            tempTask->info.timebased.nextRunTime += taskFlag & DELAY_TIME_MASK;
                        }
                    } else {
                        tempTask->info.timebased.count++;
                        tempTask->info.timebased.nextRunTime += tempTask->info.timebased.interval;
                        tempTask->execState = 0;
                    }
                    __LinkTimebasedTaskNode(taskList + currExecTaskIndex);
                    break;
                case TASKTYPE_DISPOSABLE:
                    tempTask->func(0, tempTask->execState);
                    currTimeTaskIndex = tempTask->next;
                    if (taskFlag & FLAG_DELAY_MASK) {
                        tempTask->info.timebased.nextRunTime += taskFlag & DELAY_TIME_MASK;
                        __LinkTimebasedTaskNode(taskList + currExecTaskIndex);
                    } else {
                        __ClearTaskNode(taskList + currExecTaskIndex);
                    }
                    break;
                }
            }
#ifdef IDLE_HOOK_FUNCITON
            else if (idleTask) {
                u32 currIdleTick = System_GetCurrTick();
                idleTask(currIdleTick, lastIdleTick);
                lastIdleTick = currIdleTick;
            }
#endif
        }
#ifdef AUTO_SLEEP
        else {
            System_Sleep();
        }
#endif
    }
}

#ifdef IDLE_HOOK_FUNCITON
void System_RegisterIdleTask(TaskMainFunc func)
{
    idleTask = func;
}
#endif

void System_EndLoop(void)
{
    looping = false;
}

Task *System_AddNewLoopTask(TaskMainFunc func, u32 interval)
{
    for (Task *t = taskList + TASK_MAX_NUM - 1; t >= taskList; --t) {
        if (t->func == NULL) {
            __InitTaskNode(t, TASKTYPE_CIRCULATE, func);
            t->info.timebased.nextRunTime = System_GetCurrTick() + interval;
            t->info.timebased.interval    = interval;
            __LinkTimebasedTaskNode(t);
            return t;
        }
    }
    return NULL;
}

Task *System_AddNewTempTask(TaskMainFunc func, u32 interval)
{
    for (Task *t = taskList + TASK_MAX_NUM - 1; t >= taskList; --t) {
        if (t->func == NULL) {
            __InitTaskNode(t, TASKTYPE_DISPOSABLE, func);
            t->info.timebased.nextRunTime = System_GetCurrTick() + interval;
            __LinkTimebasedTaskNode(t);
            return t;
        }
    }
    return NULL;
}

#ifdef ENABLE_EVENT_TASK
Task *System_AddNewEventTask(TaskMainFunc func, Event *event, u16 signal)
{
    if (signal == 0 || __IsEventParamInvalid(event)) {
        return NULL;
    }
    for (Task *t = taskList + TASK_MAX_NUM - 1; t >= taskList; --t) {
        if (t->func == NULL) {
            __InitTaskNode(t, TASKTYPE_EVENT, func);
            t->info.eventbased.event  = event;
            t->info.eventbased.signal = signal;

            TaskIndex j = event->subList;
            if (j == __EndOfTaskList) {
                event->subList = t->curr;
            } else {
                while (taskList[j].next != __EndOfTaskList) {
                    j = taskList[j].next;
                }
                taskList[j].next = t->curr;
            }
            return t;
        }
    }
    return NULL;
}
#endif

bool System_SuspendTask(Task *task, u16 nextState)
{
    if (__IsTaskParamInvalid(task) || task->type == TASKTYPE_DISPOSABLE) {
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
    task->execState = nextState;
    return true;
}

bool System_ResumeTask(Task *task, u16 execState, bool instance)
{
    if (__IsTaskParamInvalid(task) || task->type == TASKTYPE_DISPOSABLE) {
        return false;
    }
#ifdef ENABLE_EVENT_TASK
    if (task->type == TASKTYPE_EVENT) {
        task->info.eventbased.suspend = false;
        return true;
    }
#endif
    task->execState                  = execState;
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

bool System_SetEvent(Event *event, u16 signal, u32 value)
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

u16 System_GetEventSignal(Event *event)
{
    return event->signal;
}
#endif

bool Task_Yield(u16 nextState)
{
    if (currExecTaskIndex == __EndOfTaskList
#ifdef ENABLE_EVENT_TASK
        || taskList[currExecTaskIndex].type == TASKTYPE_EVENT
#endif
    ) {
        return false;
    }
    taskFlag &= ~DELAY_TIME_MASK;
    taskFlag |= FLAG_DELAY_MASK;
    taskList[currExecTaskIndex].info.timebased.nextRunTime = System_GetCurrTick();
    taskList[currExecTaskIndex].execState                  = nextState;
    return true;
}

bool Task_Delay(u16 ticks, u16 nextState)
{
    if (currExecTaskIndex == __EndOfTaskList) {
        return false;
    }
    taskFlag |= (ticks & DELAY_TIME_MASK);
    taskFlag |= FLAG_DELAY_MASK;
    taskList[currExecTaskIndex].execState = nextState;
    return true;
}

bool Task_Suspend(u16 nextState)
{
    if (currExecTaskIndex == __EndOfTaskList || taskList[currExecTaskIndex].type == TASKTYPE_DISPOSABLE || nextState) {
        return false;
    }
    taskFlag |= FLAG_SUSPEND_MASK;
    taskList[currExecTaskIndex].execState = nextState;
    return true;
}

#ifdef ENABLE_EVENT_TASK
bool Task_ListenSingal(u16 newSignal)
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