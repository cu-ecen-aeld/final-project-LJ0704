/*
    @file perfTest.c
    @brief Simple test for perfInterface
*/

#include <stdio.h>
#include <unistd.h>
#include "perfInterface.h"

int main(void)
{
    perfJobInfo_t job = {
        .lbaRange = {
            .startlba = 0,
            .endlba   = 204800   /* 100 MiB worth of sectors at 512B each */
        },
        .duration_ms = 5000      /* run for 5 seconds */
    };

    printf("Initializing perf system...\n");
    initPerfSystem();

    printf("Starting sequential write...\n");
    if (perfStartSeqWrite(&job) != STATUS_OK) {
        printf("Failed to start write job\n");
        return 1;
    }

    printf("Starting sequential read...\n");
    if (perfStartSeqRead(&job) != STATUS_OK) {
        printf("Failed to start read job\n");
        return 1;
    }

    printf("Done.\n");
    return 0;
}