#include <assert.h>
#include <stdio.h>
#include <stddef.h>
/* rename to avoid clashing with host libc */
#define memcpy os_memcpy
#define memmove os_memmove
#define memset os_memset
#define memcmp os_memcmp
#define memchr os_memchr
#define strlen os_strlen
#define strnlen os_strnlen
#define strcmp os_strcmp
#define strncmp os_strncmp
#define strcpy os_strcpy
#define strncpy os_strncpy
#define strcat os_strcat
#define strncat os_strncat
#define strchr os_strchr
#define strrchr os_strrchr
#define strstr os_strstr
#include "string.c"
int main(void){
  char buf[64];
  os_strcpy(buf,"hello"); assert(os_strlen(buf)==5);
  os_strcat(buf," world"); assert(os_strcmp(buf,"hello world")==0);
  assert(os_strncmp("abcd","abce",3)==0);
  assert(os_strncmp("abcd","abce",4)!=0);
  os_memset(buf,'x',3); buf[3]=0; assert(os_strcmp(buf,"xxx")==0);
  char a[10]="12345"; os_memmove(a+1,a,4); a[5]=0; assert(os_strcmp(a,"11234")==0);
  assert(*os_strchr("abcabc",'c')=='c');
  assert(*os_strrchr("abcabc",'a')=='a');
  assert(os_strstr("hello world","wor")!=NULL);
  assert(os_strstr("hello","xyz")==NULL);
  assert(os_memcmp("abc","abd",3)<0);
  assert(os_strnlen("hello",3)==3);
  char d[4]; os_strncpy(d,"ab",4); assert(d[0]=='a'&&d[1]=='b'&&d[2]==0&&d[3]==0);
  assert(*(char*)os_memchr("hello",'l',5)=='l');
  char e[8]="ab"; os_strncat(e,"cdef",2); assert(os_strcmp(e,"abcd")==0);
  printf("M5.3a string.c: ALL PASS\n");
  return 0;
}
