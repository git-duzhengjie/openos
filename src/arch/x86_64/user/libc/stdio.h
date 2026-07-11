/*
 * M5.3c libc/stdio.h — standard C stdio subset for OpenOS ring3 userland.
 *
 * Character/string output built on the SYS_WRITE syscall, plus a formatting
 * engine (printf family) and a minimal FILE abstraction with the three
 * standard streams. Freestanding: no host libc dependency.
 */
#ifndef OPENOS_LIBC_STDIO_H
#define OPENOS_LIBC_STDIO_H

#ifndef OPENOS_LIBC_SIZE_T_DEFINED
#define OPENOS_LIBC_SIZE_T_DEFINED
typedef unsigned long  size_t;
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef EOF
#define EOF (-1)
#endif

/* --- va_list: use the compiler builtins (freestanding-safe) --- */
#ifndef OPENOS_LIBC_VA_LIST_DEFINED
#define OPENOS_LIBC_VA_LIST_DEFINED
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(dst, src)  __builtin_va_copy(dst, src)
#endif

/* --- minimal FILE abstraction: just wraps a file descriptor --- */
typedef struct _openos_FILE {
    int fd;
    int error;
    int eof;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* --- unformatted output --- */
int  fputc(int c, FILE *stream);
int  putc(int c, FILE *stream);
int  putchar(int c);
int  fputs(const char *s, FILE *stream);
int  puts(const char *s);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

/* --- formatted output --- */
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int vprintf(const char *format, va_list ap);
int vfprintf(FILE *stream, const char *format, va_list ap);

#endif /* OPENOS_LIBC_STDIO_H */
