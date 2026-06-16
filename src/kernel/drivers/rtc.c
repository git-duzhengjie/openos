/* ============================================================
 * openos - CMOS RTC clock
 * ============================================================ */
#include "../include/rtc.h"
#include "../include/io.h"
#include "../include/serial.h"

#define CMOS_ADDR_PORT 0x70u
#define CMOS_DATA_PORT 0x71u
#define CMOS_NMI_DISABLE 0x80u

#define RTC_REG_SECONDS 0x00u
#define RTC_REG_MINUTES 0x02u
#define RTC_REG_HOURS   0x04u
#define RTC_REG_DAY     0x07u
#define RTC_REG_MONTH   0x08u
#define RTC_REG_YEAR    0x09u
#define RTC_REG_A       0x0Au
#define RTC_REG_B       0x0Bu

#define RTC_REG_A_UIP   0x80u
#define RTC_REG_B_24H   0x02u
#define RTC_REG_B_BINARY 0x04u

static rtc_time_t g_boot_time;
static int g_boot_time_valid;

static uint8_t rtc_cmos_read(uint8_t reg) {
    outb(CMOS_ADDR_PORT, (uint8_t)(CMOS_NMI_DISABLE | reg));
    io_wait();
    return inb(CMOS_DATA_PORT);
}

static int rtc_update_in_progress(void) {
    return (rtc_cmos_read(RTC_REG_A) & RTC_REG_A_UIP) != 0;
}

static uint8_t rtc_bcd_to_bin(uint8_t value) {
    return (uint8_t)((value & 0x0Fu) + ((value >> 4) * 10u));
}

static void rtc_print2(uint8_t value) {
    char buf[3];
    buf[0] = (char)('0' + (value / 10u));
    buf[1] = (char)('0' + (value % 10u));
    buf[2] = '\0';
    serial_write(buf);
}

static void rtc_print4(uint16_t value) {
    char buf[5];
    buf[0] = (char)('0' + ((value / 1000u) % 10u));
    buf[1] = (char)('0' + ((value / 100u) % 10u));
    buf[2] = (char)('0' + ((value / 10u) % 10u));
    buf[3] = (char)('0' + (value % 10u));
    buf[4] = '\0';
    serial_write(buf);
}

static void rtc_print_time(const rtc_time_t *time) {
    rtc_print4(time->year);
    serial_write("-");
    rtc_print2(time->month);
    serial_write("-");
    rtc_print2(time->day);
    serial_write(" ");
    rtc_print2(time->hour);
    serial_write(":");
    rtc_print2(time->minute);
    serial_write(":");
    rtc_print2(time->second);
}

const rtc_time_t *rtc_get_boot_time(void) {
    return g_boot_time_valid ? &g_boot_time : 0;
}

int rtc_read_time(rtc_time_t *out_time) {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t second2;
    uint8_t minute2;
    uint8_t hour2;
    uint8_t day2;
    uint8_t month2;
    uint8_t year2;
    uint8_t reg_b;
    uint32_t retries = 100000u;

    if (!out_time) return -1;

    while (rtc_update_in_progress() && retries-- > 0) {
        io_wait();
    }
    if (retries == 0) return -1;

    do {
        second = rtc_cmos_read(RTC_REG_SECONDS);
        minute = rtc_cmos_read(RTC_REG_MINUTES);
        hour = rtc_cmos_read(RTC_REG_HOURS);
        day = rtc_cmos_read(RTC_REG_DAY);
        month = rtc_cmos_read(RTC_REG_MONTH);
        year = rtc_cmos_read(RTC_REG_YEAR);

        while (rtc_update_in_progress()) {
            io_wait();
        }

        second2 = rtc_cmos_read(RTC_REG_SECONDS);
        minute2 = rtc_cmos_read(RTC_REG_MINUTES);
        hour2 = rtc_cmos_read(RTC_REG_HOURS);
        day2 = rtc_cmos_read(RTC_REG_DAY);
        month2 = rtc_cmos_read(RTC_REG_MONTH);
        year2 = rtc_cmos_read(RTC_REG_YEAR);
    } while (second != second2 || minute != minute2 || hour != hour2 ||
             day != day2 || month != month2 || year != year2);

    reg_b = rtc_cmos_read(RTC_REG_B);

    if ((reg_b & RTC_REG_B_BINARY) == 0) {
        second = rtc_bcd_to_bin(second);
        minute = rtc_bcd_to_bin(minute);
        hour = (uint8_t)((hour & 0x80u) | rtc_bcd_to_bin((uint8_t)(hour & 0x7Fu)));
        day = rtc_bcd_to_bin(day);
        month = rtc_bcd_to_bin(month);
        year = rtc_bcd_to_bin(year);
    }

    if ((reg_b & RTC_REG_B_24H) == 0 && (hour & 0x80u) != 0) {
        hour = (uint8_t)(((hour & 0x7Fu) + 12u) % 24u);
    }

    out_time->year = (uint16_t)(2000u + year);
    out_time->month = month;
    out_time->day = day;
    out_time->hour = hour;
    out_time->minute = minute;
    out_time->second = second;

    return 0;
}

void rtc_init(void) {
    serial_write("=====================================\n");
    serial_write("RTC Clock\n");
    serial_write("=====================================\n");

    g_boot_time_valid = (rtc_read_time(&g_boot_time) == 0);
    if (!g_boot_time_valid) {
        serial_write("[RTC] read failed\n");
        return;
    }

    serial_write("[RTC] boot time ");
    rtc_print_time(&g_boot_time);
    serial_write("\n");
    serial_write("=====================================\n");
}
