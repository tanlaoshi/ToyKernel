/*
 * SchedHostTypes.h — Host 单测里的瘦任务。只保留政策会碰的字段。
 */
#ifndef SCHED_HOST_TYPES_H
#define SCHED_HOST_TYPES_H

typedef unsigned int UINT32;
typedef int INT32;

#define HAL_MAX_CPUS 8
#define MAX_TASKS 16

typedef struct TASK {
    INT32 Affinity;
    INT32 HomeCpu;
    INT32 Priority;
    int InRunQueue;
} TASK;

#endif
