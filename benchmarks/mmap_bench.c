#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
    MMAP_WARMUP = 200,
    MMAP_SAMPLES = 2000,
    TOUCH_WARMUP = 2,
    TOUCH_SAMPLES = 40,
};

static void die(const char *operation)
{
    perror(operation);
    exit(EXIT_FAILURE);
}

static uint64_t now_ns(void)
{
    struct timespec timestamp;

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &timestamp) < 0)
        die("clock_gettime");
    return (uint64_t)timestamp.tv_sec * UINT64_C(1000000000) + (uint64_t)timestamp.tv_nsec;
}

static uint64_t sample_mmap(int fd, size_t length)
{
    uint64_t start = now_ns();
    void *region = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    uint64_t elapsed = now_ns() - start;

    if (region == MAP_FAILED)
        die("mmap");
    if (munmap(region, length) < 0)
        die("munmap");
    return elapsed;
}

static uint64_t sample_touch(int fd, size_t length, size_t page_size)
{
    const volatile unsigned char *region =
        mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (region == MAP_FAILED)
        die("mmap");

    uint64_t start = now_ns();
    for (size_t offset = 0; offset < length; offset += page_size)
        (void)region[offset];
    uint64_t elapsed = now_ns() - start;

    if (munmap((void *)region, length) < 0)
        die("munmap");
    return elapsed;
}

static int compare_samples(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;

    return (a > b) - (a < b);
}

int main(int argc, char **argv)
{
    struct stat metadata;
    struct rusage before;
    struct rusage after;
    uint64_t latency[MMAP_SAMPLES];
    uint64_t touch_ns = 0;
    long page_size;
    int fd;

    if (argc != 2) {
        fprintf(stderr, "Uso: %s ARCHIVO_DE_PRUEBA\n", argv[0]);
        return EXIT_FAILURE;
    }
    fd = open(argv[1], O_RDWR | O_CLOEXEC);
    if (fd < 0)
        die("open");
    if (fstat(fd, &metadata) < 0)
        die("fstat");
    page_size = sysconf(_SC_PAGESIZE);
    if (!S_ISREG(metadata.st_mode) || page_size <= 0 || metadata.st_size <= 0 ||
        metadata.st_size % page_size != 0 || (uintmax_t)metadata.st_size > SIZE_MAX) {
        fprintf(stderr, "El archivo debe ser regular y su tamaño un múltiplo del tamaño de página.\n");
        return EXIT_FAILURE;
    }

    const size_t length = (size_t)metadata.st_size;
    for (int i = 0; i < MMAP_WARMUP; ++i)
        sample_mmap(fd, length);
    for (int i = 0; i < MMAP_SAMPLES; ++i)
        latency[i] = sample_mmap(fd, length);
    qsort(latency, MMAP_SAMPLES, sizeof(*latency), compare_samples);
    const double median =
        (latency[MMAP_SAMPLES / 2 - 1] + latency[MMAP_SAMPLES / 2]) / 2.0;

    for (int i = 0; i < TOUCH_WARMUP; ++i)
        sample_touch(fd, length, (size_t)page_size);
    if (getrusage(RUSAGE_SELF, &before) < 0)
        die("getrusage");
    for (int i = 0; i < TOUCH_SAMPLES; ++i)
        touch_ns += sample_touch(fd, length, (size_t)page_size);
    if (getrusage(RUSAGE_SELF, &after) < 0)
        die("getrusage");
    if (close(fd) < 0)
        die("close");

    printf("%d,%.1f,%" PRIu64 ",%d,%zu,%" PRIu64 ",%ld,%ld\n",
           MMAP_SAMPLES, median, latency[(95 * MMAP_SAMPLES + 99) / 100 - 1],
           TOUCH_SAMPLES, length / (size_t)page_size, touch_ns,
           after.ru_minflt - before.ru_minflt, after.ru_majflt - before.ru_majflt);
    return EXIT_SUCCESS;
}
