/*
    @file commandQueue.h
    @Author Zane Mcmorris
    @date March 30, 2026
*/

#ifndef INTERFACE_COMMON_H
#define INTERFACE_COMMON_H

#include <stdint.h>

#define MAX_PARTITIONS (32)

typedef enum{
    ERROR_NONE = 0,
    ERROR_STATE = -1,
    ERROR_INPUT = -2,
    ERROR_MEMORY = -3,
    ERROR_EMPTY_QUEUE = -4,
    ERROR_RE_INIT = -5,
    ERROR_NO_INIT = -6,
}error_t;

typedef enum{
    STATUS_OK = 0,
    STATUS_FAIL = -1,
    STATUS_NO_JOB = -2,


}status_t;

typedef struct{
    uint32_t startlba;
    uint32_t endlba;
}lbaRange_t;



#endif