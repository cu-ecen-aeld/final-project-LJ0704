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
#define SECTOR_SIZE 512


typedef struct{
    int fd; //file descriptor for the job
    void *buf; 
    uint64_t bytes_done; //bytes completed so far
    volatile int stop; //flag to signal the job to stop
    perfJobInfo_t info; //info about the job
    int is_write; //flag to indicate if it is a write job
} job_state;

static job_state *current_job = NULL; //pointer to the currently running job, NULL if no job is running

/**
 * @brief Get the current time in milliseconds
 */
static uint64_t current_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/**
 * @brief Set up perf system. Whatever would need to be run once before we call into it repeatedly
 */
status_t initPerfSystem(){
    current_job = NULL;
    return STATUS_OK;
}

/**
 * @brief returns some sort of status/state of perf business. 
 * Probably something along the lines of (ready, fault, running (if running, print some stats?))
 */
int getPerfStatus(){
    if (current_job == NULL){
        return 0;
    }
    return 1;
}





/*

*/

static void run_job(job_state *job)
{
    uint64_t base_offset = (uint64_t)job->info.lbaRange.startlba * SECTOR_SIZE;
    uint64_t end_offset = (uint64_t)job->info.lbaRange.endlba * SECTOR_SIZE;
    uint64_t offset = base_offset;
    uint64_t start_time = current_ms();

    lseek(job->fd, (off_t)base_offset, SEEK_SET);

    while(!job->stop){
        if((job->info.duration_ms > 0) && ((current_ms() - start_time) >= job->info.duration_ms)){
            break;
        }
        // Perform I/O operation 
        if(offset + BLOCK_SIZE > end_offset){
            // If the next block would go past the end of the range, wrap around to the start
            offset = base_offset;
            lseek(job->fd, (off_t)base_offset, SEEK_SET);
        }
        ssize_t io_size;
        if(job->is_write){
            io_size = write_unaligned(job->fd, offset, offset + BLOCK_SIZE, job->buf);
            if(io_size < 0){
                fprintf(stderr, "[perf] : Write I/O error at offset %lu : %s\n", offset, strerror(errno));
                break;
            }
            offset += BLOCK_SIZE;
        } else {
            io_size = read(job->fd, job->buf, BLOCK_SIZE);
            if(io_size < 0){
                fprintf(stderr, "[perf] : Read I/O error at offset %lu : %s\n", offset, strerror(errno));
                break;
            }
            offset += (uint64_t)io_size;
            if (offset + BLOCK_SIZE > end_offset) {
                offset = base_offset;
                lseek(job->fd, (off_t)base_offset, SEEK_SET);
            }
        }

        job->bytes_done += (uint64_t)io_size;
    }

    uint64_t total_time = current_ms() - start_time;
    if(total_time == 0){
        total_time = 1; //prevent division by zero
    }

    double mb          = (double)job->bytes_done / (1024.0 * 1024.0);
    double elapsed_sec = (double)total_time / 1000.0;

    printf("[perf] %s  |  %.2f MiB  |  %.2f MB/s\n",
           job->is_write ? "SEQ WRITE" : "SEQ READ",
           mb,
           mb / elapsed_sec);

    close(job->fd);
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
    
    new_job->fd = open("/dev/nvme0n1", O_RDWR  | O_DIRECT);
    if(new_job->fd < 0){
        fprintf(stderr, "[perf] : Failed to open device for sequential writes : %s\n", strerror(errno));
        free(new_job);
        return STATUS_FAIL;
    }

    if(posix_memalign(&new_job->buf, BUFFER_ALIGN, BLOCK_SIZE) != 0){
        fprintf(stderr, "[perf] : Failed to allocate aligned buffer for sequential writes : %s\n", strerror(errno));
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

/**
 * @brief Starts listed operation, taking in info about the job
 * function name: perfStartSeqRead
 * description:   Starts a sequential read job with the given parameters. Will run until duration_ms is up or until perfStopJob is called, whichever comes first.
 * parameters:    perfJobInfo_t* info - pointer to struct containing info about the job, including LBA range and duration      
 * Returns:       STATUS_FAIL if job cannot be started for some reason (invalid input, job already running, etc)
 *                STATUS_OK if job is successfully started
 */
status_t perfStartSeqRead(perfJobInfo_t* info){
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
    new_job->is_write = SET_READ;
    
    new_job->fd = open("/dev/nvme0n1", O_RDONLY  | O_DIRECT);
    if(new_job->fd < 0){
        fprintf(stderr, "[perf] : Failed to open device for sequential reads : %s\n", strerror(errno));
        free(new_job);
        return STATUS_FAIL;
    }

    
    if(posix_memalign(&new_job->buf, BUFFER_ALIGN, BLOCK_SIZE) != 0){
        fprintf(stderr, "[perf] : Failed to allocate aligned buffer for sequential reads : %s\n", strerror(errno));
        close(new_job->fd);
        free(new_job);
        return STATUS_FAIL;
    }

    new_job->bytes_done = 0;
    new_job->stop = 0;
    current_job = new_job;

    run_job(current_job);

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

        if(current_job == NULL){
            return STATUS_NO_JOB;
        }
        current_job->stop = 1;

        free(current_job->buf);
        close(current_job->fd);
        free(current_job);
        current_job = NULL;
            
    return STATUS_OK;
}