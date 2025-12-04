



#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define PMU_CTRL_PATH  "/proc/pmu_control"
#define PMU_STATS_PATH "/proc/pmu_stats"

#define N 512  

struct pmu_stats {
    unsigned long long instructions;
    unsigned long long l1i_ref;
    unsigned long long l1i_miss;
    unsigned long long l1d_ref;
    unsigned long long l1d_miss;
    unsigned long long llc_miss;
    unsigned long long cycles;
};

static int pmu_control(const char *cmd)
{
    int fd = open(PMU_CTRL_PATH, O_WRONLY);
    if (fd < 0) {
        perror("open pmu_control");
        return -1;
    }
    ssize_t len = strlen(cmd);
    if (write(fd, cmd, len) != len) {
        perror("write pmu_control");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int pmu_read_stats(struct pmu_stats *s)
{
    char buf[512];
    int fd = open(PMU_STATS_PATH, O_RDONLY);
    if (fd < 0) {
        perror("open pmu_stats");
        return -1;
    }

    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        perror("read pmu_stats");
        close(fd);
        return -1;
    }
    buf[n] = '\0';
    close(fd);

    int matched = sscanf(buf,
                         "instructions: %llu\n"
                         "l1i_references: %llu\n"
                         "l1i_misses: %llu\n"
                         "l1d_references: %llu\n"
                         "l1d_misses: %llu\n"
                         "llc_misses: %llu\n"
                         "cycles: %llu",
                         &s->instructions,
                         &s->l1i_ref,
                         &s->l1i_miss,
                         &s->l1d_ref,
                         &s->l1d_miss,
                         &s->llc_miss,
                         &s->cycles);

    if (matched != 7) {
        fprintf(stderr, "Failed to parse pmu_stats (matched=%d)\n", matched);
        return -1;
    }
    return 0;
}

static void print_stats(const char *label, const struct pmu_stats *s)
{
    printf("==== PMU statistics for %s ====\n", label);
    printf("instructions : %llu\n", s->instructions);
    printf("l1i_ref      : %llu\n", s->l1i_ref);
    printf("l1i_miss     : %llu\n", s->l1i_miss);
    printf("l1d_ref      : %llu\n", s->l1d_ref);
    printf("l1d_miss     : %llu\n", s->l1d_miss);
    printf("llc_miss     : %llu\n", s->llc_miss);
    printf("cycles       : %llu\n\n", s->cycles);
}

int main(void)
{
    double *A, *B, *C;
    struct pmu_stats init_stats, mm_stats;
    long long checksum = 0;
    int i, j, k;

    size_t bytes = (size_t)N * N * sizeof(double);
    A = malloc(bytes);
    B = malloc(bytes);
    C = malloc(bytes);

    if (!A || !B || !C) {
        perror("malloc");
        free(A); free(B); free(C);
        return 1;
    }

    printf("Matrix size: %dx%d, each %.2f MB (total ~%.2f MB)\n",
           N, N,
           (double)bytes / (1024.0 * 1024.0),
           3.0 * (double)bytes / (1024.0 * 1024.0));

    
    printf("[Phase 1] Initializing matrices A and B...\n");

    if (pmu_control("start\n") < 0) goto out;

    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            A[i * N + j] = (double)(i + j);
            B[i * N + j] = (double)(i == j ? 1.0 : 0.0);  
            C[i * N + j] = 0.0;
        }
    }

    if (pmu_control("stop\n") < 0) goto out;
    if (pmu_read_stats(&init_stats) < 0) goto out;

    print_stats("Phase 1 (matrix initialization)", &init_stats);

    
    printf("[Phase 2] Performing matrix multiplication C = A * B...\n");

    if (pmu_control("start\n") < 0) goto out;

    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            double sum = 0.0;
            for (k = 0; k < N; k++) {
                sum += A[i * N + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }

    if (pmu_control("stop\n") < 0) goto out;
    if (pmu_read_stats(&mm_stats) < 0) goto out;

    print_stats("Phase 2 (matrix multiplication)", &mm_stats);

    
    for (i = 0; i < N; i++)
        checksum += (long long)C[i * N + (i % N)];
    printf("Checksum: %lld\n", checksum);

out:
    free(A); free(B); free(C);
    return 0;
}
