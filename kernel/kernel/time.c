#include <kernel/time.h>
#include <kernel/io.h>

static int tz_minutes = 0;

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static int bcd_to_bin(int v)
{
    return (v & 0x0F) + ((v >> 4) * 10);
}

static int is_leap(int y)
{
    if ((y % 4) != 0)
        return 0;
    if ((y % 100) != 0)
        return 1;
    return (y % 400) == 0;
}

static int days_in_month(int y, int m)
{
    static const int dm[12] = { 31, 28, 31, 30, 31, 30,
                                31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12)
        return 31;
    if (m == 2 && is_leap(y))
        return 29;
    return dm[m - 1];
}

static void add_days(os_time_t* t, int days)
{
    while (days > 0) {
        t->day++;
        if (t->day > days_in_month(t->year, t->month)) {
            t->day = 1;
            t->month++;
            if (t->month > 12) {
                t->month = 1;
                t->year++;
            }
        }
        days--;
    }
    while (days < 0) {
        t->day--;
        if (t->day < 1) {
            t->month--;
            if (t->month < 1) {
                t->month = 12;
                t->year--;
            }
            t->day = days_in_month(t->year, t->month);
        }
        days++;
    }
}

void time_utc(os_time_t* t)
{
    uint32_t guard = 0;
    int sec = 0, min = 0, hour = 0, day = 0, mon = 0, year = 0;
    int sec2, min2, hour2, day2, mon2, year2;

    while ((cmos_read(0x0A) & 0x80) && guard++ < 1000000) {}

    for (int attempt = 0; attempt < 8; attempt++) {
        sec = cmos_read(0x00);
        min = cmos_read(0x02);
        hour = cmos_read(0x04);
        day = cmos_read(0x07);
        mon = cmos_read(0x08);
        year = cmos_read(0x09);

        sec2 = cmos_read(0x00);
        min2 = cmos_read(0x02);
        hour2 = cmos_read(0x04);
        day2 = cmos_read(0x07);
        mon2 = cmos_read(0x08);
        year2 = cmos_read(0x09);

        if (sec == sec2 && min == min2 && hour == hour2 &&
            day == day2 && mon == mon2 && year == year2)
            break;
    }

    uint8_t regB = cmos_read(0x0B);
    int pm = 0;

    if (!(regB & 0x02) && (hour & 0x80)) {
        pm = 1;
        hour &= 0x7F;
    }
    if (!(regB & 0x04)) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hour = bcd_to_bin(hour);
        day = bcd_to_bin(day);
        mon = bcd_to_bin(mon);
        year = bcd_to_bin(year);
    }
    if (!(regB & 0x02)) {
        if (pm && hour < 12)
            hour += 12;
        if (!pm && hour == 12)
            hour = 0;
    }

    int century = bcd_to_bin(cmos_read(0x32));
    int full_year = (century >= 19 && century <= 99) ? century * 100 + year
                                                     : 2000 + year;

    if (mon < 1 || mon > 12) mon = 1;
    if (day < 1 || day > 31) day = 1;
    if (hour > 23) hour = 0;
    if (min > 59) min = 0;
    if (sec > 59) sec = 0;

    t->year = full_year;
    t->month = mon;
    t->day = day;
    t->hour = hour;
    t->minute = min;
    t->second = sec;
}

void time_add_minutes(const os_time_t* in, int minutes, os_time_t* out)
{
    *out = *in;
    int total = out->hour * 60 + out->minute + minutes;
    int days = 0;
    while (total >= 1440) {
        total -= 1440;
        days++;
    }
    while (total < 0) {
        total += 1440;
        days--;
    }
    out->hour = total / 60;
    out->minute = total % 60;
    add_days(out, days);
}

void time_local(os_time_t* t)
{
    os_time_t utc;
    time_utc(&utc);
    time_add_minutes(&utc, tz_minutes, t);
}

int time_tz_offset(void)
{
    return tz_minutes;
}

void time_set_tz_offset(int minutes)
{
    if (minutes < -12 * 60)
        minutes = -12 * 60;
    if (minutes > 14 * 60)
        minutes = 14 * 60;
    tz_minutes = minutes;
}

void time_format_tz(int minutes, char* out, int max)
{
    if (max < 10)
        return;

    int n = 0;
    out[n++] = 'U';
    out[n++] = 'T';
    out[n++] = 'C';
    if (minutes < 0) {
        out[n++] = '-';
        minutes = -minutes;
    } else {
        out[n++] = '+';
    }
    int hh = minutes / 60;
    int mm = minutes % 60;
    if (hh >= 10) {
        out[n++] = (char)('0' + hh / 10);
        out[n++] = (char)('0' + hh % 10);
    } else {
        out[n++] = (char)('0' + hh);
    }
    if (mm) {
        out[n++] = ':';
        out[n++] = (char)('0' + mm / 10);
        out[n++] = (char)('0' + mm % 10);
    }
    out[n] = '\0';
}
