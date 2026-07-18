/* =========================================================================
 * multitouch_selftest64.h -- M8-C.4 boot-time selftest declaration.
 * ========================================================================= */
#ifndef OPENOS_MULTITOUCH_SELFTEST64_H
#define OPENOS_MULTITOUCH_SELFTEST64_H
#ifdef __cplusplus
extern "C" {
#endif
/* Runs both hid_parser and gesture_multi tests, prints PASS/FAIL to console
 * and klog. Returns 0 on success. */
int arch_x86_64_multitouch_selftest_run(void);
#ifdef __cplusplus
}
#endif
#endif
