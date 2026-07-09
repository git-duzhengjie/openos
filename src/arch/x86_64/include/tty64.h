/*
 * tty64.h — M4.4a: pseudo-terminal line discipline engine.
 *
 * A tty64 device is a bidirectional character conduit sitting between an
 * input source (keyboard / master write) and a reader (a ring3 program's
 * read() on the terminal). Its job is the classic UNIX "line discipline":
 *
 *   - COOKED / canonical mode (ICANON): input is accumulated in an edit
 *     buffer and only handed to read() one *line* at a time (terminated by
 *     '\n' or EOF). ERASE (^H / DEL) rubs out the last char, KILL (^U) wipes
 *     the whole pending line. With ECHO on, every accepted char is mirrored
 *     to the output ring so the user sees what they type.
 *
 *   - RAW mode (ICANON cleared): every byte is made available to read()
 *     immediately, no editing, no line buffering. ECHO still honoured.
 *
 * Special control chars in canonical mode:
 *     INTR  (^C)  -> flush line, raise a pending-signal flag (SIGINT)
 *     EOF   (^D)  -> terminate the current line; on an empty line yields a
 *                    zero-length read (classic end-of-file)
 *     ERASE (^H / 0x7F) / KILL (^U) -> in-line editing
 *
 * The engine is deliberately free of any dependency on the process model or
 * the syscall layer: it is a pure byte machine with two rings (input-cooked
 * and output/echo) plus a small termios-like flag block. That makes it fully
 * exercisable from the syscall self-test with no ring3 program in the loop.
 * Job control (process groups, SIGINT delivery to the foreground pgrp) is
 * layered on top in M4.4b via the pgrp field + tty64_take_signal().
 */
#ifndef OPENOS_ARCH_X86_64_TTY64_H
#define OPENOS_ARCH_X86_64_TTY64_H

#include <stdint.h>
#include <stddef.h>

#define TTY64_MAX          4       /* max simultaneous tty devices        */
#define TTY64_IBUF         512     /* raw input ring capacity             */
#define TTY64_CBUF         512     /* cooked (line-assembled) ring cap    */
#define TTY64_OBUF         512     /* output / echo ring capacity         */
#define TTY64_LINE_MAX     256     /* max chars in one canonical line     */

/* c_lflag bits (subset of POSIX termios local flags). */
#define TTY64_ICANON       0x0002u /* canonical (line) input              */
#define TTY64_ECHO         0x0008u /* echo input chars to output          */
#define TTY64_ISIG         0x0001u /* INTR/QUIT/SUSP generate signals     */

/* Control-char slots (indices into c_cc[]). */
enum {
    TTY64_VINTR = 0,   /* ^C */
    TTY64_VEOF,        /* ^D */
    TTY64_VERASE,      /* ^H / DEL */
    TTY64_VKILL,       /* ^U */
    TTY64_NCCS         /* count */
};

/* Window size (TIOCGWINSZ / TIOCSWINSZ). */
typedef struct tty64_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} tty64_winsize_t;

/* termios-like control block exposed via TCGETS/TCSETS. */
typedef struct tty64_termios {
    uint32_t c_lflag;              /* local flags (ICANON/ECHO/ISIG)      */
    uint8_t  c_cc[TTY64_NCCS];     /* control characters                  */
} tty64_termios_t;

/* Lifecycle ---------------------------------------------------------- */

/* Reset the whole table; allocate nothing. Call at init. */
void tty64_reset(void);

/* Create a tty device with default cooked-mode termios (ICANON|ECHO|ISIG,
 * standard control chars). Returns a tty id (>=0) or -1 on table full. */
int  tty64_create(void);

/* True if id refers to a live tty. */
int  tty64_valid(int id);

/* Input path ---------------------------------------------------------- */

/* Feed one raw input byte (as if typed on the keyboard / written to the
 * master). Runs it through the line discipline: in canonical mode it may be
 * buffered/edited/echoed and only committed to the cooked ring on line
 * termination; in raw mode it lands in the cooked ring immediately. Returns
 * 0 on success, -1 on bad id. A control char that triggers a signal is
 * recorded (see tty64_take_signal) and not stored. */
int  tty64_input_byte(int id, uint8_t ch);

/* Convenience: feed a NUL-terminated string byte by byte. Returns count. */
int  tty64_input_str(int id, const char *s);

/* Read up to len cooked bytes into buf. In canonical mode returns whole
 * lines only (may return <len if a line ends sooner, 0 at EOF on empty
 * line). In raw mode returns whatever is available up to len. Returns the
 * byte count (>=0), or -1 on bad id. Non-blocking: returns 0 if nothing is
 * ready (blocking is the caller/syscall layer's job). */
int  tty64_read(int id, void *buf, int len);

/* How many cooked bytes are ready to read right now (whole-line aware in
 * canonical mode: counts only up to and including complete lines). */
int  tty64_readable(int id);

/* Output path --------------------------------------------------------- */

/* Write len bytes to the output ring (program -> terminal). Returns bytes
 * accepted (may be <len if the ring fills), or -1 on bad id. */
int  tty64_write(int id, const void *buf, int len);

/* Drain up to len bytes of pending output (echo + program writes) into buf,
 * e.g. for the console renderer. Returns byte count, -1 on bad id. */
int  tty64_drain_output(int id, void *buf, int len);

/* Bytes pending in the output ring. */
int  tty64_output_pending(int id);

/* termios / ioctl backing ------------------------------------------- */

/* TCGETS: copy the current termios into *out. Returns 0 / -1. */
int  tty64_tcgets(int id, tty64_termios_t *out);

/* TCSETS: install *in as the new termios. Switching ICANON off flushes the
 * pending edit line into the cooked ring. Returns 0 / -1. */
int  tty64_tcsets(int id, const tty64_termios_t *in);

/* TIOCGWINSZ / TIOCSWINSZ. */
int  tty64_get_winsize(int id, tty64_winsize_t *out);
int  tty64_set_winsize(int id, const tty64_winsize_t *in);

/* Signal handshake (M4.4b bridges this to the foreground pgrp) --------- */

/* If a control char (^C/^D...) queued a signal, return it (>0, e.g. SIGINT)
 * and clear the latch; otherwise return 0. */
int  tty64_take_signal(int id);

/* Foreground process-group id associated with this tty (TIOCGPGRP/TIOCSPGRP).
 * 0 means "unset". Job control in M4.4b reads/writes these. */
int  tty64_get_pgrp(int id);
int  tty64_set_pgrp(int id, int pgrp);

/* Diagnostics: number of live ttys. */
int  tty64_active_count(void);

#endif /* OPENOS_ARCH_X86_64_TTY64_H */
