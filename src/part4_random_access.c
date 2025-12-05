



#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#define PMU_CTRL_PATH  "/proc/pmu_control"
#define PMU_STATS_PATH "/proc/pmu_stats"

#define ARRAY_SIZE (16 * 4 * 1024 * 1024)  
#define RANDOM_ITERS (4 * ARRAY_SIZE)

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
    int *arr;
    struct pmu_stats seq_stats, rand_stats;
    long long sum = 0;
    size_t i;

    arr = malloc(sizeof(int) * ARRAY_SIZE);
    if (!arr) {
        perror("malloc");
        return 1;
    }

    printf("[Init] Filling array sequentially...\n");
    for (i = 0; i < ARRAY_SIZE; i++)
        arr[i] = (int)i;

    printf("Array size: %zu ints (%.1f MB)\n",
           (size_t)ARRAY_SIZE,
           (double)ARRAY_SIZE * sizeof(int) / (1024.0 * 1024.0));

    
    printf("[Phase 1] Sequential scan...\n");

    if (pmu_control("start\n") < 0) goto out;
    for (i = 0; i < ARRAY_SIZE; i++)
        sum += arr[i];
    if (pmu_control("stop\n") < 0) goto out;

    if (pmu_read_stats(&seq_stats) < 0) goto out;
    print_stats("Phase 1 (sequential access)", &seq_stats);

    
    printf("[Phase 2] Random access...\n");
    srand((unsigned)time(NULL));

    if (pmu_control("start\n") < 0) goto out;
    for (i = 0; i < RANDOM_ITERS; i++) {
        size_t idx = (size_t) (rand() % ARRAY_SIZE);
        sum += arr[idx];
    }
    if (pmu_control("stop\n") < 0) goto out;

    if (pmu_read_stats(&rand_stats) < 0) goto out;
    print_stats("Phase 2 (random access)", &rand_stats);

    printf("Final sum (to avoid optimization): %lld\n", sum);

out:
    free(arr);
    return 0;
}
