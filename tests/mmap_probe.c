#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

static int select_uid(const char *text)
{
    uintmax_t parsed;
    char *end;

    if (!*text || text[strspn(text, "0123456789")]) {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    parsed = strtoumax(text, &end, 10);
    if (errno || *end || parsed == 0 || parsed >= (uid_t)-1) {
        errno = EINVAL;
        return -1;
    }

    const uid_t uid = (uid_t)parsed;
    if (getuid() == uid && geteuid() == uid)
        return 0;
    if (setgroups(0, NULL) || setgid((gid_t)uid) || setuid(uid))
        return -1;
    return 0;
}

static int check_call(const char *operation, int actual, int expected)
{
    if (actual != expected) {
        fprintf(stderr, "%s uid=%ju: se esperaba %s (%d), se obtuvo %s (%d)\n",
                operation, (uintmax_t)getuid(), strerror(expected), expected,
                strerror(actual), actual);
        return EXIT_FAILURE;
    }

    printf("%s uid=%ju resultado=%d\n", operation, (uintmax_t)getuid(), actual);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    int flags;
    int expected;
    bool change_protection;
    bool shared;
    int fd;
    long page_size;
    int actual;

    if (argc != 5) {
        fprintf(stderr, "Uso: %s ARCHIVO UID shared|validate|private|mprotect|readonly|write|existing ok|deny|dac\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    change_protection = strcmp(argv[3], "mprotect") == 0;
    const bool readonly = strcmp(argv[3], "readonly") == 0;
    const bool direct_write = strcmp(argv[3], "write") == 0;
    const bool existing = strcmp(argv[3], "existing") == 0;
    shared = strcmp(argv[3], "private") != 0;
    if (strcmp(argv[3], "validate") == 0)
        flags = MAP_SHARED_VALIDATE;
    else if (strcmp(argv[3], "shared") == 0 || change_protection || readonly || direct_write || existing)
        flags = MAP_SHARED;
    else if (!shared)
        flags = MAP_PRIVATE;
    else {
        fprintf(stderr, "Tipo de mapeo inválido.\n");
        return EXIT_FAILURE;
    }
    if (strcmp(argv[4], "ok") == 0)
        expected = 0;
    else if (strcmp(argv[4], "deny") == 0)
        expected = EPERM;
    else if (strcmp(argv[4], "dac") == 0)
        expected = EACCES;
    else {
        fprintf(stderr, "Resultado esperado inválido.\n");
        return EXIT_FAILURE;
    }

    if (select_uid(argv[2]) < 0) {
        perror("Cambiar a UID de prueba");
        return EXIT_FAILURE;
    }

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        fprintf(stderr, "No se obtuvo el tamaño de página.\n");
        return EXIT_FAILURE;
    }
    fd = open(argv[1], (readonly ? O_RDONLY : O_RDWR) | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    if (direct_write) {
        ssize_t written = write(fd, "D", 1);
        actual = written < 0 ? errno : (written == 1 ? 0 : EIO);
        if (!actual && fsync(fd) < 0)
            actual = errno;
        if (close(fd) < 0 && !actual)
            actual = errno;
        return check_call("write", actual, expected);
    }

    const size_t length = (size_t)page_size;
    const int prot = change_protection ? PROT_READ : PROT_READ | PROT_WRITE;
    char *region = mmap(NULL, length, prot, flags, fd, 0);

    if (region == MAP_FAILED) {
        actual = errno;
        close(fd);
        if (change_protection) {
            fprintf(stderr, "Falló el mmap inicial: %s\n", strerror(actual));
            return EXIT_FAILURE;
        }
        return check_call("mmap", actual, expected);
    }

    actual = 0;
    if (change_protection && mprotect(region, length, PROT_READ | PROT_WRITE) < 0)
        actual = errno;
    int status = check_call(change_protection ? "mprotect" : "mmap", actual, expected);
    if (!actual && !status) {
        if (existing && (puts("MAPPING_READY") == EOF || fflush(stdout) == EOF || getchar() != 'W')) {
            fprintf(stderr, "No se recibió la señal para usar el mapeo existente.\n");
            status = EXIT_FAILURE;
        } else {
            ((volatile char *)region)[0] = existing ? 'E' : 'B';
            if (shared && msync(region, length, MS_SYNC) < 0) {
                perror("msync");
                status = EXIT_FAILURE;
            }
        }
    }
    if (munmap(region, length) < 0) {
        perror("munmap");
        status = EXIT_FAILURE;
    }
    if (close(fd) < 0) {
        perror("close");
        status = EXIT_FAILURE;
    }
    return status;
}
