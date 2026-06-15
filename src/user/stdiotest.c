#include "openos.h"

int main(int argc, char **argv)
{
    FILE *fp;
    char buf[64];
    int n;
    char msg[48];

    (void)argc;
    (void)argv;

    snprintf(msg, sizeof(msg), "stdio %s %d", "ok", 42);
    printf("[stdiotest] %s\n", msg);

    fp = fopen("/tmp/stdio.txt", "w");
    if (!fp) {
        fprintf(stderr, "[stdiotest] fopen write failed errno=%d\n", errno);
        return 1;
    }
    fprintf(fp, "line=%s\nnum=%d\n", "hello", 123);
    fputs("tail\n", fp);
    fclose(fp);

    fp = fopen("/tmp/stdio.txt", "r");
    if (!fp) {
        fprintf(stderr, "[stdiotest] fopen read failed errno=%d\n", errno);
        return 1;
    }
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = 0;
    fclose(fp);

    printf("[stdiotest] read %d bytes:\n%s", n, buf);
    if (openos_strncmp(buf, "line=hello\nnum=123\ntail\n", 24) != 0) {
        fprintf(stderr, "[stdiotest] content mismatch\n");
        return 1;
    }

    printf("[stdiotest] OK\n");
    return 0;
}
