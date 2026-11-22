#ifndef MMAP_GUARD_H
#define MMAP_GUARD_H

struct file_identity {
    __u32 device_major;
    __u32 device_minor;
    __u64 inode;
};

struct guard_config {
    struct file_identity file;
    __u32 allowed_uid;
    __u32 audit;
};

enum guard_operation {
    GUARD_MMAP = 1,
    GUARD_MPROTECT = 2,
};

struct guard_event {
    struct file_identity file;
    __u64 timestamp_ns;
    __u64 requested_prot;
    __u64 effective_prot;
    __u64 flags;
    __u32 pid;
    __u32 uid;
    __u32 operation;
    __s32 decision;
};

_Static_assert(sizeof(struct file_identity) == 16, "invalid file identity layout");
_Static_assert(sizeof(struct guard_config) == 24, "invalid guard configuration layout");
_Static_assert(sizeof(struct guard_event) == 64, "invalid audit event layout");

#endif
