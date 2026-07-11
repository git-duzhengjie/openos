/*
 * M5.3d libc/ctype.h — character classification/conversion for OpenOS userland.
 *
 * ASCII-only ("C" locale) semantics. Declared here, implemented in ctype.c.
 * Per the C standard the argument must be representable as unsigned char or
 * equal to EOF; passing other values is undefined.
 */
#ifndef OPENOS_LIBC_CTYPE_H
#define OPENOS_LIBC_CTYPE_H

int isalnum(int c);
int isalpha(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);

#endif /* OPENOS_LIBC_CTYPE_H */
