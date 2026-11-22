#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/types.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "mmap_guard.h"
#include "mmap_guard_limits.h"
#include "mmap_guard.skel.h"

_Static_assert(PROT_WRITE == GUARD_PROT_WRITE, "invalid PROT_WRITE constant");
_Static_assert(MAP_TYPE == GUARD_MAP_TYPE, "invalid MAP_TYPE constant");
_Static_assert(MAP_SHARED == GUARD_MAP_SHARED, "invalid MAP_SHARED constant");
_Static_assert(MAP_SHARED_VALIDATE == GUARD_MAP_SHARED_VALID,
               "invalid MAP_SHARED_VALIDATE constant");
_Static_assert(EPERM == GUARD_EPERM, "invalid EPERM constant");

static volatile sig_atomic_t stopping;

static void stop(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static int parse_uid(const char *text, __u32 *uid)
{
    char *end;
    uintmax_t value;

    if (!*text || text[strspn(text, "0123456789")])
        return -1;
    errno = 0;
    value = strtoumax(text, &end, 10);
    if (errno || *end || value >= UINT32_MAX)
        return -1;
    *uid = (__u32)value;
    return 0;
}

static int audit_event(void *context, void *data, size_t length)
{
    const struct guard_event *event = data;
    const char *operation;

    (void)context;
    if (length != sizeof(*event))
        return -EMSGSIZE;

    operation = event->operation == GUARD_MMAP ? "mmap" : "mprotect";
    printf("event=%s time_ns=%" PRIu64 " pid=%" PRIu32 " uid=%" PRIu32
           " file=%" PRIu32 ":%" PRIu32 ":%" PRIu64
           " req=0x%" PRIx64 " prot=0x%" PRIx64 " flags=0x%" PRIx64
           " decision=%s status=%" PRId32 "\n",
           operation, (uint64_t)event->timestamp_ns, (uint32_t)event->pid,
           (uint32_t)event->uid, (uint32_t)event->file.device_major,
           (uint32_t)event->file.device_minor, (uint64_t)event->file.inode,
           (uint64_t)event->requested_prot, (uint64_t)event->effective_prot,
           (uint64_t)event->flags, event->decision ? "deny" : "pass",
           (int32_t)event->decision);
    return fflush(stdout) == EOF ? -EIO : 0;
}

static int set_active(int fd, __u32 value)
{
    const __u32 key = 0;

    return bpf_map_update_elem(fd, &key, &value, BPF_ANY);
}

int main(int argc, char **argv)
{
    struct sigaction action = { .sa_handler = stop };
    struct guard_config settings = { .audit = 1 };
    const __u32 zero = 0;
    struct mmap_guard_bpf *guard = NULL;
    struct ring_buffer *ring = NULL;
    struct stat metadata;
    __u64 count;
    int file = -1;
    int active_fd = -1;
    int result = EXIT_FAILURE;
    int error;

    if ((argc != 3 && argc != 4) || parse_uid(argv[2], &settings.allowed_uid) ||
        (argc == 4 && strcmp(argv[3], "--no-audit") != 0)) {
        fprintf(stderr, "Uso: %s ARCHIVO UID_AUTORIZADO [--no-audit]\n", argv[0]);
        return EXIT_FAILURE;
    }
    settings.audit = argc == 3;

    file = open(argv[1], O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (file < 0) {
        perror("open");
        goto done;
    }
    if (fstat(file, &metadata) < 0) {
        perror("fstat");
        goto done;
    }
    if (!S_ISREG(metadata.st_mode)) {
        fprintf(stderr, "El objetivo debe ser un archivo regular.\n");
        goto done;
    }
    settings.file = (struct file_identity){
        .device_major = major(metadata.st_dev),
        .device_minor = minor(metadata.st_dev),
        .inode = metadata.st_ino,
    };

    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction");
        goto done;
    }

    guard = mmap_guard_bpf__open();
    if (!guard) {
        perror("Abrir objeto eBPF");
        goto done;
    }
    guard->rodata->guard_settings = settings;
    error = mmap_guard_bpf__load(guard);
    if (error) {
        fprintf(stderr, "Cargar objeto eBPF: %s\n", strerror(-error));
        goto done;
    }

    ring = ring_buffer__new(bpf_map__fd(guard->maps.events), audit_event, NULL, NULL);
    if (!ring) {
        perror("Abrir búfer de auditoría");
        goto done;
    }
    error = mmap_guard_bpf__attach(guard);
    if (error) {
        fprintf(stderr, "Conectar ganchos BPF LSM: %s\n", strerror(-error));
        goto done;
    }

    active_fd = bpf_map__fd(guard->maps.active);
    if (set_active(active_fd, 1)) {
        perror("Activar política");
        goto done;
    }
    puts("READY");
    if (fflush(stdout) == EOF) {
        perror("Anunciar activación");
        goto done;
    }

    while (!stopping) {
        error = ring_buffer__poll(ring, 200);
        if (error < 0 && error != -EINTR) {
            fprintf(stderr, "Leer auditoría: %s\n", strerror(-error));
            goto done;
        }
    }
    result = EXIT_SUCCESS;

done:
    if (active_fd >= 0) {
        if (set_active(active_fd, 0)) {
            perror("Desactivar política");
            result = EXIT_FAILURE;
        }
        mmap_guard_bpf__detach(guard);
        error = ring_buffer__consume(ring);
        if (error < 0) {
            fprintf(stderr, "Finalizar auditoría: %s\n", strerror(-error));
            result = EXIT_FAILURE;
        }
        if (bpf_map_lookup_elem(bpf_map__fd(guard->maps.dropped), &zero, &count)) {
            perror("Leer pérdidas de auditoría");
            result = EXIT_FAILURE;
        } else {
            printf("audit_dropped=%" PRIu64 "\n", (uint64_t)count);
        }
    }
    ring_buffer__free(ring);
    mmap_guard_bpf__destroy(guard);
    if (file >= 0)
        close(file);
    return result;
}
