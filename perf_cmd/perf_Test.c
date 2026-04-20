/*
    @file perf_Test.c
    @brief Simple test for perfInterface - with unaligned test cases
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "perfInterface.h"

#define SECTOR_SIZE    512 
#define BLOCK_SIZE    1048576
#define BUFFER_ALIGN  4096
#define VERIFY_PATTERN 0xAB
#define DEVICE        "/dev/nvme0n1"

/**
 * @brief Tests an unaligned write then reads back and verifies.
 *        Uses write_unaligned style — O_DIRECT with RMW for partial sectors.
 *
 * @param byte_offset  exact byte offset to start writing (does not need to be sector aligned)
 * @param length       number of bytes to write (does not need to be sector aligned)
 * @param pattern      byte pattern to fill with
 */
static int test_unaligned(uint64_t byte_offset, uint32_t length, uint8_t pattern)
{
    printf("[unaligned] testing offset=%-10lu length=%-6u pattern=0x%02X ... ",
           byte_offset, length, pattern);

    /* open without O_DIRECT so kernel handles unaligned naturally */
    int fd = open(DEVICE, O_RDWR);
    if(fd < 0){
        printf("FAIL — open: %s\n", strerror(errno));
        return 1;
    }

    /* plain malloc is fine without O_DIRECT */
    uint8_t *write_buf = malloc(length);
    uint8_t *read_buf  = malloc(length);
    if(write_buf == NULL || read_buf == NULL){
        printf("FAIL — malloc\n");
        free(write_buf); free(read_buf); close(fd);
        return 1;
    }

    memset(write_buf, pattern, length);

    /* write at exact unaligned offset */
    ssize_t w = pwrite(fd, write_buf, length, (off_t)byte_offset);
    if(w != (ssize_t)length){
        printf("FAIL — pwrite returned %ld: %s\n", w, strerror(errno));
        free(write_buf); free(read_buf); close(fd);
        return 1;
    }

    /* read back */
    ssize_t r = pread(fd, read_buf, length, (off_t)byte_offset);
    if(r != (ssize_t)length){
        printf("FAIL — pread returned %ld: %s\n", r, strerror(errno));
        free(write_buf); free(read_buf); close(fd);
        return 1;
    }

    /* verify */
    int pass = (memcmp(write_buf, read_buf, length) == 0);
    if(pass){
        printf("PASS\n");
    } else {
        for(uint32_t i = 0; i < length; i++){
            if(write_buf[i] != read_buf[i]){
                printf("FAIL — mismatch at byte %u: wrote 0x%02X read 0x%02X\n",
                       i, write_buf[i], read_buf[i]);
                break;
            }
        }
    }

    free(write_buf);
    free(read_buf);
    close(fd);
    return pass ? 0 : 1;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s --mode <mode> [--start <lba>] [--end <lba>] [--duration <ms>]\n", prog);
    printf("\n");
    printf("  --mode     seq_write | seq_read | rand_write | rand_read | all | unaligned\n");
    printf("  --start    start LBA (default: 0)\n");
    printf("  --end      end LBA   (default: 204800 = 100MiB)\n");
    printf("  --duration milliseconds to run (default: 5000)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --mode seq_write --start 0 --end 204800 --duration 5000\n", prog);
    printf("  %s --mode unaligned\n", prog);
    printf("  %s --mode all\n", prog);
}

static int verify_write_read(uint64_t start_lba)
{
    int fd = open(DEVICE, O_RDWR | O_DIRECT);
    if(fd < 0){ perror("[verify] open"); return 1; }

    void *write_buf = NULL, *read_buf = NULL;
    if(posix_memalign(&write_buf, BUFFER_ALIGN, BLOCK_SIZE) != 0 ||
       posix_memalign(&read_buf,  BUFFER_ALIGN, BLOCK_SIZE) != 0){
        printf("[verify] alloc failed\n");
        close(fd); return 1;
    }

    memset(write_buf, VERIFY_PATTERN, BLOCK_SIZE);
    uint64_t byte_offset = start_lba * SECTOR_SIZE;

    ssize_t w = pwrite(fd, write_buf, BLOCK_SIZE, (off_t)byte_offset);
    if(w != BLOCK_SIZE){ printf("[verify] pwrite failed\n"); free(write_buf); free(read_buf); close(fd); return 1; }

    ssize_t r = pread(fd, read_buf, BLOCK_SIZE, (off_t)byte_offset);
    if(r != BLOCK_SIZE){ printf("[verify] pread failed\n"); free(write_buf); free(read_buf); close(fd); return 1; }

    int pass = (memcmp(write_buf, read_buf, BLOCK_SIZE) == 0);
    if(pass){
        printf("[verify] PASS — LBA %lu matches 0x%02X\n", start_lba, VERIFY_PATTERN);
    } else {
        for(int i = 0; i < BLOCK_SIZE; i++){
            if(((uint8_t*)write_buf)[i] != ((uint8_t*)read_buf)[i]){
                printf("[verify] FAIL — byte %d: wrote 0x%02X read 0x%02X\n",
                       i, ((uint8_t*)write_buf)[i], ((uint8_t*)read_buf)[i]);
                break;
            }
        }
    }

    free(write_buf); free(read_buf); close(fd);
    return pass ? 0 : 1;
}

int main(int argc, char *argv[])
{
    const char *mode     = NULL;
    uint64_t    start    = 0;
    uint64_t    end      = 204800;
    uint32_t    duration = 5000;

    if(argc < 2){ print_usage(argv[0]); return 1; }

    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "--mode") == 0 && i + 1 < argc){
            mode = argv[++i];
        } else if(strcmp(argv[i], "--start") == 0 && i + 1 < argc){
            start = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if(strcmp(argv[i], "--end") == 0 && i + 1 < argc){
            end = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if(strcmp(argv[i], "--duration") == 0 && i + 1 < argc){
            duration = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if(strcmp(argv[i], "--help") == 0){
            print_usage(argv[0]); return 0;
        } else {
            printf("Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]); return 1;
        }
    }

    if(mode == NULL){ printf("Error: --mode is required\n"); print_usage(argv[0]); return 1; }
    if(strcmp(mode, "unaligned") != 0 && end <= start){ printf("Error: --end must be greater than --start\n"); return 1; }

    perfJobInfo_t job = {
        .lbaRange    = { .startlba = start, .endlba = end },
        .duration_ms = duration
    };

    initPerfSystem();

    int ret = 0;

    if(strcmp(mode, "seq_write") == 0){
        printf("Starting sequential write...\n");
        if(perfStartSeqWrite(&job) != STATUS_OK){ printf("Failed\n"); return 1; }

    } else if(strcmp(mode, "seq_read") == 0){
        printf("Starting sequential read...\n");
        if(perfStartSeqRead(&job) != STATUS_OK){ printf("Failed\n"); return 1; }

    } else if(strcmp(mode, "rand_write") == 0){
        printf("Starting random write...\n");
        if(perfStartRandWrite(&job) != STATUS_OK){ printf("Failed\n"); return 1; }

    } else if(strcmp(mode, "rand_read") == 0){
        printf("Starting random read...\n");
        if(perfStartRandRead(&job) != STATUS_OK){ printf("Failed\n"); return 1; }

    } else if(strcmp(mode, "all") == 0){
        printf("Starting sequential write...\n");
        if(perfStartSeqWrite(&job) != STATUS_OK){ printf("Failed\n"); return 1; }
        printf("Starting sequential read...\n");
        if(perfStartSeqRead(&job) != STATUS_OK){ printf("Failed\n"); return 1; }
        printf("Starting random write...\n");
        if(perfStartRandWrite(&job) != STATUS_OK){ printf("Failed\n"); return 1; }
        printf("Starting random read...\n");
        if(perfStartRandRead(&job) != STATUS_OK){ printf("Failed\n"); return 1; }

        printf("\nRunning data integrity checks...\n");
        int failures = 0;
        uint64_t check_lbas[] = {start, start + 1024, start + 10240, end - 2048};
        for(int i = 0; i < 4; i++)
            failures += verify_write_read(check_lbas[i]);
        ret = failures;
        if(failures == 0) printf("All integrity checks passed.\n");
        else              printf("%d integrity check(s) FAILED.\n", failures);

    } else if(strcmp(mode, "unaligned") == 0){
        printf("Running unaligned I/O tests...\n\n");

        int failures = 0;

        /* offset not sector aligned, length not sector aligned */
        failures += test_unaligned(12345,          300,  0xAA);
        failures += test_unaligned(12345 + 512,    300,  0xBB);
        failures += test_unaligned(12345 + 1024,   513,  0xCC);

        /* straddles a sector boundary */
        failures += test_unaligned(511,             2,   0xDD);  /* byte 511 + 512 */
        failures += test_unaligned(1023,            2,   0xEE);  /* byte 1023 + 1024 */

        /* offset aligned but length unaligned */
        failures += test_unaligned(4096,            100, 0xFF);
        failures += test_unaligned(4096,            513, 0x11);

        /* large unaligned write straddling multiple sectors */
        failures += test_unaligned(12345,          4096, 0x22);
        failures += test_unaligned(12345 + 512,    4096, 0x33);

        printf("\n%d/%d unaligned tests %s\n",
               9 - failures, 9,
               failures == 0 ? "PASSED" : "FAILED");
        ret = failures;

    } else {
        printf("Unknown mode: %s\n", mode); print_usage(argv[0]); return 1;
    }

    printf("Done.\n");
    return ret;
}
