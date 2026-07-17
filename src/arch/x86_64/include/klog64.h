/*
 * klog64.h -- M6.12 kernel ring buffer logger.
 *
 * Defines the kernel logging facility:
 *   - Circular ring buffer: 64KB default, log entry overhead ~20 bytes
 *   - Syslog-compatible levels: EMERG/ALERT/CRIT/ERR/WARN/NOTICE/INFO/DEBUG
 *   - Facilities: KERNEL/VM/SCHED/NET/VFS/GUI/USER
 *   - Userspace query via SYS_KLOG (487)
 *
 * Integration: early_console64_write() tee's all output into klog
 * automatically; all existing [x86_64] prefixed logs are captured.
 */
#ifndef KLOG64_H
#define KLOG64_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ---------------------- syslog levels (RFC 5424) ---------------------- */
#define KLOG_EMERG    0   /* system is unusable */
#define KLOG_ALERT    1   /* action must be taken immediately */
#define KLOG_CRIT     2   /* critical conditions */
#define KLOG_ERR      3   /* error conditions */
#define KLOG_WARN     4   /* warning conditions */
#define KLOG_NOTICE   5   /* normal but significant condition */
#define KLOG_INFO     6   /* informational */
#define KLOG_DEBUG    7   /* debug-level messages */
#define KLOG_LEVEL_MASK 0x7

/* ---------------------- facility codes (RFC 5424-ish) ---------------------- */
#define KLOG_FAC_KERNEL   0  /* kernel messages */
#define KLOG_FAC_VM       1  /* virtual memory / address space */
#define KLOG_FAC_SCHED    2  /* scheduler / process management */
#define KLOG_FAC_NET      3  /* network stack */
#define KLOG_FAC_VFS      4  /* virtual file system */
#define KLOG_FAC_GUI      5  /* graphics / window manager */
#define KLOG_FAC_USER     6  /* userspace messages */
#define KLOG_FAC_SHIFT    3
#define KLOG_MAKE_PRIO(level, fac) (((fac) << KLOG_FAC_SHIFT) | ((level) & KLOG_LEVEL_MASK))

/* ---------------------- SYS_KLOG command codes ---------------------- */
#define KLOG_CMD_READ_ALL    0  /* read all entries starting from seq=0 */
#define KLOG_CMD_READ_TAIL   1  /* read last N entries (a1 = count) */
#define KLOG_CMD_READ_FROM   2  /* read from seq >= a1 (resume after dropped data) */
#define KLOG_CMD_STATS       3  /* get statistics (a1 = klog_stats_t*) */
#define KLOG_CMD_CLEAR       4  /* clear the ring buffer (requires uid=0) */

/* ---------------------- ring buffer constants ---------------------- */
#define KLOG_BUFFER_SIZE     (64u << 10)  /* 64KB */
#define KLOG_ENTRY_MAGIC     0x4B4C4731u  /* "KLG1" */

/* ---------------------- in-memory entry header ---------------------- */
/* Each log entry is laid out in the ring buffer as:
 *   [hdr: 20 bytes] [msg: N bytes] [NUL: 1 byte]
 * NUL is guaranteed so msg is always a valid C string.
 * The next entry starts at ALIGN8(hdr.len) after this header.
 */
typedef struct klog_entry {
    uint32_t  magic;      /* KLOG_ENTRY_MAGIC */
    uint32_t  seq;        /* monotonic sequence number (never wraps) */
    uint64_t  timestamp;  /* TSC at emission time (ns if TSC freq known) */
    uint16_t  prio;       /* level | (fac << 3) */
    uint16_t  len;        /* msg length NOT including NUL (total bytes = len+1) */
    char      msg[];      /* NUL-terminated message */
} __attribute__((packed)) klog_entry_t;

/* ---------------------- statistics structure ---------------------- */
typedef struct klog_stats {
    uint32_t  seq_next;       /* next sequence number to assign */
    uint32_t  seq_oldest;     /* oldest sequence still in buffer (== seq_next if empty) */
    uint32_t  total_emitted;  /* total entries emitted since boot */
    uint32_t  total_dropped;  /* total entries dropped due to wrap */
    uint32_t  buffer_used;    /* current bytes used in ring */
    uint32_t  buffer_size;    /* KLOG_BUFFER_SIZE */
} __attribute__((packed)) klog_stats_t;

/* ---------------------- public API ---------------------- */
void klog_init(void);
void klog_emit(uint8_t level, uint8_t facility, const char *msg);
bool klog_read_from(uint32_t start_seq, char *buf, size_t bufsize, size_t *out_len);
bool klog_read_tail(uint32_t count, char *buf, size_t bufsize, size_t *out_len);
void klog_get_stats(klog_stats_t *out);
void klog_clear(void);  /* requires uid=0 when called from syscall */

/* No klogf/klog printf-wrapper exposed on purpose: the kernel has no
 * vsnprintf outside libc. Instead, early_console64_write() tees every
 * line-terminated segment into klog_emit() automatically, so all
 * existing [x86_64] prefixed diagnostics are captured for free. Code
 * that wants a specific level/facility calls klog_emit() directly. */

#endif
