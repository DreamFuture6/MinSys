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

#define __EndOfEvtList ((EvtIndex)(-1))

typedef struct Event {
    u32 value;
    u16 signal;
    bool enable;
    TaskIndex subList;
} Event;

static Event eventList[EVENT_MAX_NUM];
static EvtIndex eventQueue[EVENT_MAX_NUM];
static EvtHandle *endEvent = eventList + EVENT_MAX_NUM - 1;
#endif

typedef enum TaskType {
    TASKTYPE_CIRCULATE  = 0x01,
    TASKTYPE_DISPOSABLE = 0x02,
    TASKTYPE_EVENT      = 0x04,
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
    TaskIndex next;
    TaskType type;
    TaskMainFunc func;
    TaskInfo info;
};

static bool looping;
static u16 taskFlag; // [0~7]:delay time [8]:delay [9]:close [10]:suspend
#ifdef IDLE_HOOK_FUNCTION
static TaskMainFunc idleTask;
#endif

static TaskIndex currTimeTaskIndex, currExecTaskIndex;
static Task taskList[TASK_MAX_NUM];
static TaskHandle *endTask = taskList + TASK_MAX_NUM - 1;

#define __EndOfTaskList   ((TaskIndex)(-1))
#define DELAY_TIME_MASK   ((u16)((1U << 8) - 1))
#define FLAG_DELAY_MASK   ((u16)(1U << 8))
#define FLAG_CLOSE_MASK   ((u16)(1U << 9))
#define FLAG_SUSPEND_MASK ((u16)(1U << 10))
#define FLAG_YIELD_MASK   ((u16)(1U << 11))

#define __TaskIndex(task) (task - taskList)

static inline bool __IsTaskParamInvalid(TaskHandle *task)
{
    return task == NULL || task < taskList || task > endTask || taskList[__TaskIndex(task)].func == NULL;
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
        currTimeTaskIndex = __TaskIndex(task);
    } else {
        taskList[prev].next = __TaskIndex(task);
        task->next          = curr;
    }
}

static inline bool __SetNextNodeOfPrevTaskNode(Task *task, TaskIndex startNode)
{
    TaskIndex prev = __EndOfTaskList, curr = startNode;
    while (curr != __TaskIndex(task)) {
        prev = curr;
        curr = taskList[curr].next;
        if (curr == __EndOfTaskList) {
            return true;
        }
    }
    if (prev == __EndOfTaskList) {
        return false;
    }
    taskList[prev].next = task->next;
    return false;
}

static inline void __ResetTaskExecuteEnv(void)
{
    taskFlag = 0x0000;
}

#ifdef ENABLE_EVENT_TASK
static inline bool __IsEventParamInvalid(EvtHandle *event)
{
    return event == NULL || event < eventList || event > endEvent || event->enable == false;
}

static inline void __DeleteEventTask(Task *task)
{
    Event *e = task->info.eventbased.event;
    if (e->subList == __TaskIndex(task)) {
        e->subList = task->next;
    } else {
        __SetNextNodeOfPrevTaskNode(task, e->subList);
    }
    __ClearTaskNode(task);
}

static void __SystemEventHandlerTask(u32 count, u16 state)
{
    TaskIndex prev = __EndOfTaskList, curr = currExecTaskIndex;
    for (EvtIndex ei = (EvtIndex)state; ei < EVENT_MAX_NUM; ++ei) {
        if (eventList[ei].enable) {
            prev = eventList[ei].subList;
            while (prev != __EndOfTaskList) {
                if (taskList[prev].info.eventbased.nextRunTime && taskList[prev].info.eventbased.nextRunTime <= System_GetCurrTick()) {
                    currExecTaskIndex = prev;
                    __ResetTaskExecuteEnv();
                    taskList[prev].func(0, 0);
                    taskList[prev].info.eventbased.nextRunTime = 0;
                    if (taskFlag) {
                        if (taskFlag & FLAG_CLOSE_MASK) {
                            __DeleteEventTask(taskList + prev);
                        } else if (taskFlag & FLAG_SUSPEND_MASK) {
                            taskList[prev].info.eventbased.suspend = true;
                        } else if (taskFlag & FLAG_DELAY_MASK) {
                            taskList[prev].info.eventbased.nextRunTime = System_GetCurrTick() + (taskFlag & DELAY_TIME_MASK);
                        }
                    }
                    currExecTaskIndex = curr;
                    Task_Yield(ei);
                    return;
                }
                prev = taskList[prev].next;
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
        taskList[i].next = __EndOfTaskList;
        taskList[i].info = (TaskInfo){0};
    }
#ifdef IDLE_HOOK_FUNCTION
    idleTask = NULL;
#endif
#ifdef ENABLE_EVENT_TASK
    eventQueue[0] = __EndOfEvtList;
    System_AddNewTimeTask(__SystemEventHandlerTask, 1, false);
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
#ifdef IDLE_HOOK_FUNCTION
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

#ifdef IDLE_HOOK_FUNCTION
void System_RegisterIdleTask(TaskMainFunc func)
{
    idleTask = func;
}
#endif

void System_EndLoop(void)
{
    looping = false;
}

TaskHandle *System_AddNewTimeTask(TaskMainFunc func, u32 interval, bool disposable)
{
    for (Task *t = taskList + TASK_MAX_NUM - 1; t >= taskList; --t) {
        if (t->func == NULL) {
            __InitTaskNode(t, disposable ? TASKTYPE_DISPOSABLE : TASKTYPE_CIRCULATE, func);
            t->info.timebased.interval    = interval;
            t->info.timebased.nextRunTime = System_GetCurrTick() + interval;
            __LinkTimebasedTaskNode(t);
            return t;
        }
    }
    return NULL;
}

#ifdef ENABLE_EVENT_TASK
TaskHandle *System_AddNewEventTask(TaskMainFunc func, EvtHandle *event, u16 signal)
{
    if (signal == 0 || __IsEventParamInvalid(event)) {
        return NULL;
    }
    for (Task *t = taskList + TASK_MAX_NUM - 1; t >= taskList; --t) {
        if (t->func == NULL) {
            __InitTaskNode(t, TASKTYPE_EVENT, func);
            t->info.eventbased.event  = (Event *)event;
            t->info.eventbased.signal = signal;

            TaskIndex j = event->subList;
            if (j == __EndOfTaskList) {
                ((Event *)event)->subList = __TaskIndex(t);
            } else {
                while (taskList[j].next != __EndOfTaskList) {
                    j = taskList[j].next;
                }
                taskList[j].next = __TaskIndex(t);
            }
            return t;
        }
    }
    return NULL;
}
#endif

bool System_SuspendTask(TaskHandle *task, u16 nextState)
{
    if (__IsTaskParamInvalid(task) || task->type == TASKTYPE_DISPOSABLE) {
        return false;
    }
    if (currExecTaskIndex == __TaskIndex(task)) {
        taskFlag |= FLAG_SUSPEND_MASK;
        ((Task *)task)->execState = nextState;
        return true;
    }
#ifdef ENABLE_EVENT_TASK
    if (task->type == TASKTYPE_EVENT) {
        ((Task *)task)->info.eventbased.suspend = true;
        return true;
    }
#endif
    if (__SetNextNodeOfPrevTaskNode((Task *)task, currTimeTaskIndex)) {
        return false;
    }
    ((Task *)task)->info.timebased.nextRunTime = 0;
    ((Task *)task)->next                       = __EndOfTaskList;
    ((Task *)task)->execState                  = nextState;
    return true;
}

bool System_ResumeTask(TaskHandle *task, u16 execState, bool instance)
{
    if (__IsTaskParamInvalid(task) || task->type == TASKTYPE_DISPOSABLE) {
        return false;
    }
#ifdef ENABLE_EVENT_TASK
    if (task->type == TASKTYPE_EVENT) {
        if (((Task *)task)->info.eventbased.suspend) {
            return false;
        }
        ((Task *)task)->info.eventbased.suspend = false;
        return true;
    }
#endif
    if (((Task *)task)->info.timebased.nextRunTime != 0) {
        return false;
    }
    ((Task *)task)->execState                  = execState;
    ((Task *)task)->info.timebased.nextRunTime = System_GetCurrTick() + (instance ? 0 : task->info.timebased.interval);
    __LinkTimebasedTaskNode(task);
    return true;
}

bool System_KillTask(TaskHandle *task)
{
    if (__IsTaskParamInvalid(task)) {
        return false;
    }
    if (currExecTaskIndex == __TaskIndex(task)) {
        taskFlag |= FLAG_CLOSE_MASK;
        return true;
    }
    switch (task->type) {
    case TASKTYPE_CIRCULATE:
    case TASKTYPE_DISPOSABLE: {
        if (currTimeTaskIndex == __EndOfTaskList) {
            return false;
        }
        if (__SetNextNodeOfPrevTaskNode((Task *)task, currTimeTaskIndex)) {
            return false;
        }
        __ClearTaskNode((Task *)task);
        return true;
    }
#ifdef ENABLE_EVENT_TASK
    case TASKTYPE_EVENT:
        if (task->info.eventbased.event->subList == __EndOfTaskList) {
            return false;
        }
        __DeleteEventTask((Task *)task);
        return true;
#endif
    default:
        return false;
    }
}

#ifdef ENABLE_EVENT_TASK
EvtHandle *System_CreateEvent(void)
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

bool System_DeleteEvent(EvtHandle *event)
{
    if (__IsEventParamInvalid(event) || event->subList != __EndOfTaskList) {
        return false;
    }
    ((Event *)event)->enable = false;
    return true;
}

bool System_SetEvent(EvtHandle *event, u16 signal, u32 value)
{
    if (__IsEventParamInvalid(event) || signal == 0 || event->signal == signal || (currExecTaskIndex != __EndOfTaskList && taskList[currExecTaskIndex].type == TASKTYPE_EVENT && taskList[currExecTaskIndex].info.eventbased.event == event)) {
        return false;
    }
    ((Event *)event)->signal = signal;
    ((Event *)event)->value  = value;
    for (EvtIndex i = 0; i < EVENT_MAX_NUM; ++i) {
        if (eventQueue[i] == __EndOfEvtList) {
            eventQueue[i] = (EvtIndex)(event - eventList);
            if (i + 1 < EVENT_MAX_NUM) {
                eventQueue[i + 1] = __EndOfEvtList;
            }
            return true;
        } else if (eventList + eventQueue[i] == event) {
            return true;
        }
    }
    return false;
}

u16 System_GetEventSignal(EvtHandle *event)
{
    if (__IsEventParamInvalid(event)) {
        return 0;
    }
    return ((Event *)event)->signal;
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
    taskFlag                              = (taskFlag & (~DELAY_TIME_MASK)) | (ticks & DELAY_TIME_MASK) | FLAG_DELAY_MASK;
    taskList[currExecTaskIndex].execState = nextState;
    return true;
}

bool Task_Suspend(u16 nextState)
{
    if (currExecTaskIndex == __EndOfTaskList || taskList[currExecTaskIndex].type == TASKTYPE_DISPOSABLE) {
        return false;
    }
    taskFlag |= FLAG_SUSPEND_MASK;
    taskList[currExecTaskIndex].execState = nextState;
    return true;
}

#ifdef ENABLE_EVENT_TASK
bool Task_ListenSignal(u16 newSignal)
{
    if (currExecTaskIndex == __EndOfTaskList || taskList[currExecTaskIndex].type != TASKTYPE_EVENT || newSignal == 0) {
        return false;
    }
    taskList[currExecTaskIndex].info.eventbased.signal = newSignal;
    return true;
}
#endif

bool Task_Close(void)
{
    if (currExecTaskIndex == __EndOfTaskList) {
        return false;
    }
    taskFlag |= FLAG_CLOSE_MASK;
    return true;
}