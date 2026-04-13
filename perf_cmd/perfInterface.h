/*
    @file perfInterface.c
    @Author Zane Mcmorris & Likhita Jonnakuti
    @date April 1, 2026

    @brief Exposes functionality for the performance testing interface for the NVME application.

*/

#ifndef PERF_INTERFACE_COMMON_H
#define PERF_INTERFACE_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "interfaceCommon.h"

typedef struct{
    lbaRange_t lbaRange;
    uint32_t duration_ms;
    // blah blah

}perfJobInfo_t;

/**
 * @brief returns some sort of status/state of perf business. 
 * Probably something along the lines of (ready, fault, running (if running, print some stats?))
 */
int getPerfStatus();

/**
 * @brief Set up perf system. Whatever would need to be run once before we call into it repeatedly
 */
status_t initPerfSystem();

/**
 * @brief Starts listed operation, taking in info about the job
 */
status_t perfStartSeqWrite(perfJobInfo_t* info);
status_t perfStartSeqRead(perfJobInfo_t* info);
status_t perfStartRandWrite(perfJobInfo_t *info);
status_t perfStartRandRead(perfJobInfo_t* info);

/**
 * @brief Stops running job. Returns STATUS_NO_JOB if nothing is running
 */
status_t perfStopJob();

#endif