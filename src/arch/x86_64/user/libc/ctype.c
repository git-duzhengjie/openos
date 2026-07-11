/*
 * M5.3d libc/ctype.c — ASCII "C" locale character routines for OpenOS userland.
 *
 * Straightforward range checks; no lookup table to keep the footprint minimal
 * and the behaviour obvious. Freestanding: no host libc dependency.
 */
#include "ctype.h"

int isdigit(int c)  { return c >= '0' && c <= '9'; }
int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
int islower(int c)  { return c >= 'a' && c <= 'z'; }
int isalpha(int c)  { return isupper(c) || islower(c); }
int isalnum(int c)  { return isalpha(c) || isdigit(c); }

int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}

int isblank(int c)  { return c == ' ' || c == '\t'; }

/* printable range is 0x20..0x7e; graph excludes the space */
int isprint(int c)  { return c >= 0x20 && c <= 0x7e; }
int isgraph(int c)  { return c > 0x20 && c <= 0x7e; }
int iscntrl(int c)  { return (c >= 0 && c < 0x20) || c == 0x7f; }

int ispunct(int c)  { return isgraph(c) && !isalnum(c); }

int tolower(int c)  { return isupper(c) ? c + ('a' - 'A') : c; }
int toupper(int c)  { return islower(c) ? c - ('a' - 'A') : c; }
