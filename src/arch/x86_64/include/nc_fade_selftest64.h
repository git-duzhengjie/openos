#ifndef OPENOS_ARCH_X86_64_NC_FADE_SELFTEST64_H
#define OPENOS_ARCH_X86_64_NC_FADE_SELFTEST64_H

#include "../../../kernel/include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M10.8 selftest: notification-center TTL fade-out.
 *
 * Stages:
 *   1) push with ttl=0 -> never expires; nc_tick() at large T keeps alpha=255
 *   2) push with ttl>0 -> before ttl: alpha stays 255, fade_start_ms=0
 *   3) at ttl boundary -> fade_start_ms latched, stat_notif_expired++
 *   4) mid-fade -> alpha strictly monotonic decreasing, 0<alpha<255
 *   5) fade end -> notification auto-removed (active=0, alpha=0),
 *      stat_notif_evicted++, notif_count decremented
 *   6) mixed batch: ttl=0 survives while ttl>0 fades away, count consistent
 *   7) idempotent tick: same now_ms twice -> no double-count
 *   8) fade_duration override honoured (custom duration_ms respected)
 */
bool arch_x86_64_nc_fade_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif
