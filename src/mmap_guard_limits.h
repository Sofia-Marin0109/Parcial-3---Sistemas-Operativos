#ifndef MMAP_GUARD_LIMITS_H
#define MMAP_GUARD_LIMITS_H

/* vmlinux.h supplies BTF types. Macros are checked against Linux headers
 * by the target build and kept separate from the userspace mmap API.
 */
#define GUARD_PROT_WRITE       0x02UL
#define GUARD_MAP_TYPE         0x0fUL
#define GUARD_MAP_SHARED       0x01UL
#define GUARD_MAP_SHARED_VALID 0x03UL
#define GUARD_VM_MAYSHARE      0x80UL
#define GUARD_DEV_MINOR_BITS   20U
#define GUARD_EPERM            1

#endif
