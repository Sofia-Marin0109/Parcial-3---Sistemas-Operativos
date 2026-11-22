// SPDX-License-Identifier: GPL-2.0-only
#include "vmlinux.h"
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "mmap_guard.h"
#include "mmap_guard_limits.h"

const volatile struct guard_config guard_settings = {};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} active SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} dropped SEC(".maps");

static __always_inline void audit(__u32 operation, __u32 uid, int decision,
                                  unsigned long reqprot, unsigned long prot,
                                  unsigned long flags)
{
    struct guard_event *event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event) {
        __u32 key = 0;
        __u64 *count = bpf_map_lookup_elem(&dropped, &key);
        if (count)
            __sync_fetch_and_add(count, 1);
        return;
    }

    event->file.device_major = guard_settings.file.device_major;
    event->file.device_minor = guard_settings.file.device_minor;
    event->file.inode = guard_settings.file.inode;
    event->timestamp_ns = bpf_ktime_get_ns();
    event->requested_prot = reqprot;
    event->effective_prot = prot;
    event->flags = flags;
    event->pid = bpf_get_current_pid_tgid() >> 32;
    event->uid = uid;
    event->operation = operation;
    event->decision = decision;
    bpf_ringbuf_submit(event, 0);
}

static __always_inline int check_file(struct file *file, __u32 operation,
                                      unsigned long reqprot, unsigned long prot,
                                      unsigned long flags, bool shared)
{
    const __u32 minor_mask = (1U << GUARD_DEV_MINOR_BITS) - 1;
    __u32 key = 0;
    const __u32 *enabled;
    struct inode *inode;
    __u32 device;
    __u32 uid;
    int decision;

    if (!file)
        return 0;

    enabled = bpf_map_lookup_elem(&active, &key);
    if (!enabled || !*enabled)
        return 0;

    inode = BPF_CORE_READ(file, f_inode);
    if (!inode || BPF_CORE_READ(inode, i_ino) != guard_settings.file.inode)
        return 0;

    device = BPF_CORE_READ(inode, i_sb, s_dev);
    if ((device >> GUARD_DEV_MINOR_BITS) != guard_settings.file.device_major ||
        (device & minor_mask) != guard_settings.file.device_minor)
        return 0;

    uid = (__u32)bpf_get_current_uid_gid();
    decision = 0;
    if (shared && (prot & GUARD_PROT_WRITE) && uid != guard_settings.allowed_uid)
        decision = -GUARD_EPERM;

    if (guard_settings.audit)
        audit(operation, uid, decision, reqprot, prot, flags);

    return decision;
}

SEC("lsm/mmap_file")
int BPF_PROG(guard_mmap, struct file *file, unsigned long reqprot,
             unsigned long prot, unsigned long flags, int ret)
{
    unsigned long type = flags & GUARD_MAP_TYPE;

    (void)ctx;

    if (ret)
        return ret;
    return check_file(file, GUARD_MMAP, reqprot, prot, flags,
                      type == GUARD_MAP_SHARED || type == GUARD_MAP_SHARED_VALID);
}

SEC("lsm/file_mprotect")
int BPF_PROG(guard_mprotect, struct vm_area_struct *vma, unsigned long reqprot,
             unsigned long prot, int ret)
{
    unsigned long flags;

    (void)ctx;

    if (ret)
        return ret;

    flags = BPF_CORE_READ(vma, vm_flags);
    return check_file(BPF_CORE_READ(vma, vm_file), GUARD_MPROTECT, reqprot,
                      prot, flags, (flags & GUARD_VM_MAYSHARE) != 0);
}

char LICENSE[] SEC("license") = "GPL";
