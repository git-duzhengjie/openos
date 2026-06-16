#ifndef RTC_H
#define RTC_H

#include <types.h>

typedef struct rtc_time {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_time_t;

void rtc_init(void);
int rtc_read_time(rtc_time_t *out_time);
const rtc_time_t *rtc_get_boot_time(void);

#endif /* RTC_H */
