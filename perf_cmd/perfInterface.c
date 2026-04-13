/*
    @file perfInterface.c
    @Author Zane Mcmorris & Likhita Jonnakuti
    @date April 1, 2026
*/

#include "perfInterface.h"

#define SET_WRITE 1
#define SET_READ 0
#define BUFFER_ALIGN 4096 
#define BLOCK_SIZE 1048576 //1MB buffer size for I/O operations

typedef struct{
    int fd; //file descriptor for the job
    void *buf; 
    uint32_t bytes_done; //bytes completed so far
    volatile int stop; //flag to signal the job to stop
    perfJobInfo_t info; //info about the job
    int is_write; //flag to indicate if it is a write job
} job_state;

static job_state *current_job = NULL; //pointer to the currently running job, NULL if no job is running

/**
 * @brief returns some sort of status/state of perf business. 
 * Probably something along the lines of (ready, fault, running (if running, print some stats?))
 */
int getPerfStatus(){
    
    return 0;
}

/**
 * @brief Set up perf system. Whatever would need to be run once before we call into it repeatedly
 */
status_t initPerfSystem(){

    return STATUS_OK;
}

/**
 * @brief Starts listed operation, taking in info about the job
 * function name: perfStartSeqWrite
 * description:   Starts a sequential write job with the given parameters. Will run until duration_ms is up or until perfStopJob is called, whichever comes first.
 * parameters:    perfJobInfo_t* info - pointer to struct containing info about the job, including LBA range and duration      
 * Returns:       STATUS_FAIL if job cannot be started for some reason (invalid input, job already running, etc)
 *                STATUS_OK if job is successfully started
 */
status_t perfStartSeqWrite(perfJobInfo_t* info){
    if(info == NULL){
        return STATUS_FAIL;
    }
    if(current_job != NULL){
        return STATUS_FAIL;
    }
    if(info->lbaRange.endlba <= info->lbaRange.startlba){
        return STATUS_FAIL;
    }

    job_state *new_job = calloc(1, sizeof(*new_job));
    if(new_job == NULL){
        return STATUS_FAIL;
    }

    new_job->info = *info;
    new_job->is_write = SET_WRITE;
    
    new_job->fd = open("/dev/nvme0n1", O_WRONLY | O_DIRECT);
    if(new_job->fd < 0){
        fprintf(stderr, "Failed to open device for sequential writes : %s\n", strerror(errno));
        free(new_job);
        return STATUS_FAIL;
    }

    if(posix_memalign(&new_job->buf, BUFFER_ALIGN, BLOCK_SIZE) != 0){
        fprintf(stderr, "Failed to allocate aligned buffer for sequential writes : %s\n", strerror(errno));
        close(new_job->fd);
        free(new_job);
        return STATUS_FAIL;
    }

    memset(new_job->buf, 0xAB, BLOCK_SIZE);

    new_job->bytes_done = 0;
    new_job->stop = 0;
    current_job = new_job;

    run_job(current_job);

    return STATUS_OK;
}

status_t perfStartSeqRead(perfJobInfo_t* info){
    if(info == NULL){
        return STATUS_FAIL;
    }

    return STATUS_OK;
}
status_t perfStartRandWrite(perfJobInfo_t *info){
    if(info == NULL){
        return STATUS_FAIL;
    }

    return STATUS_OK;
}

status_t perfStartRandRead(perfJobInfo_t* info){
    if(info == NULL){
        return STATUS_FAIL;
    }

    return STATUS_OK;
}

/**
 * @brief Stops running job. Returns STATUS_NO_JOB if nothing is running
 */
status_t perfStopJob(){

    return STATUS_OK;
}