/*
    @file perfInterface.c
    @Author Zane Mcmorris & Likhita Jonnakuti
    @date April 1, 2026
*/

#define _GNU_SOURCE

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

/***********************/
/**
 * @brief Performs a Read-Modify-Write on a single sector.
 *
 * Reads the sector at sector_off, patches bytes [patch_start, patch_end)
 * with src, then writes the sector back.
 *
 * @param fd          open file descriptor
 * @param sector_off  byte offset of the sector on device (must be SECTOR_SIZE aligned)
 * @param patch_start byte offset within sector to begin patching
 * @param patch_end   byte offset within sector to end patching (exclusive)
 * @param src         source data to patch in
 * @return            number of bytes patched on success, -1 on error
 */
static ssize_t rmw_sector(int fd, uint64_t sector_off,
                           uint32_t patch_start, uint32_t patch_end,
                           const void *src)
{
    void *sector_buf;
    if (posix_memalign(&sector_buf, BUFFER_ALIGN, SECTOR_SIZE) != 0) {
        fprintf(stderr, "[perf] rmw_sector: alloc failed\n");
        return -1;
    }

    ssize_t ret = pread(fd, sector_buf, SECTOR_SIZE, (off_t)sector_off);
    if (ret != SECTOR_SIZE) {
        fprintf(stderr, "[perf] rmw_sector: pread failed: %s\n", strerror(errno));
        free(sector_buf);
        return -1;
    }

    uint32_t patch_len = patch_end - patch_start;
    memcpy((uint8_t *)sector_buf + patch_start, src, patch_len);

    ret = pwrite(fd, sector_buf, SECTOR_SIZE, (off_t)sector_off);
    if (ret != SECTOR_SIZE) {
        fprintf(stderr, "[perf] rmw_sector: pwrite failed: %s\n", strerror(errno));
        free(sector_buf);
        return -1;
    }

    free(sector_buf);
    return (ssize_t)patch_len;
}

/**
 * @brief Writes data handling unaligned head and tail sectors via RMW.
 *
 * Layout:
 *   [start_byte .......................................... end_byte)
 *   |-- head RMW --|-- aligned bulk writes --|-- tail RMW --|
 *
 * @param fd         open file descriptor
 * @param start_byte byte offset to begin writing
 * @param end_byte   byte offset to stop writing (exclusive)
 * @param src        source buffer
 * @return           total bytes written on success, -1 on error
 */
static ssize_t write_unaligned(int fd, uint64_t start_byte, uint64_t end_byte,
                                const void *src)
{
    if (start_byte >= end_byte) return 0;

    const uint8_t *data   = src;
    uint64_t       total  = 0;
    uint64_t       cursor = start_byte;

    /* ── head: partial first sector ─────────────────────────────────────── */
    uint32_t head_off = cursor % SECTOR_SIZE;
    if (head_off != 0) {
        uint64_t sector_base     = cursor - head_off;
        uint32_t bytes_in_sector = SECTOR_SIZE - head_off;
        uint32_t patch_len       = (uint32_t)(end_byte - cursor);
        if (patch_len > bytes_in_sector)
            patch_len = bytes_in_sector;

        ssize_t done = rmw_sector(fd, sector_base,
                                  head_off, head_off + patch_len,
                                  data);
        if (done < 0) return -1;

        total  += (uint64_t)done;
        cursor += (uint64_t)done;
        data   += done;
    }

    /* ── middle: full aligned sectors ───────────────────────────────────── */
    while (cursor + SECTOR_SIZE <= end_byte) {
        uint64_t remaining = end_byte - cursor;
        uint64_t chunk     = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        chunk = (chunk / SECTOR_SIZE) * SECTOR_SIZE;
        if (chunk == 0) break;

        void *aligned_buf;
        if (posix_memalign(&aligned_buf, BUFFER_ALIGN, chunk) != 0) {
            fprintf(stderr, "[perf] write_unaligned: alloc failed\n");
            return -1;
        }

        memcpy(aligned_buf, data, chunk);

        ssize_t ret = pwrite(fd, aligned_buf, chunk, (off_t)cursor);
        free(aligned_buf);

        if (ret < 0) {
            fprintf(stderr, "[perf] write_unaligned: pwrite failed: %s\n", strerror(errno));
            return -1;
        }

        total  += (uint64_t)ret;
        cursor += (uint64_t)ret;
        data   += ret;
    }

    /* ── tail: partial last sector ──────────────────────────────────────── */
    if (cursor < end_byte) {
        uint32_t patch_len = (uint32_t)(end_byte - cursor);

        ssize_t done = rmw_sector(fd, cursor,
                                  0, patch_len,
                                  data);
        if (done < 0) return -1;

        total += (uint64_t)done;
    }

    return (ssize_t)total;
}

/* ***************************/

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
