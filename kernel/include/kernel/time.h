#ifndef TIME_H
#define TIME_H

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} os_time_t;

void time_utc(os_time_t* t);
void time_local(os_time_t* t);
void time_add_minutes(const os_time_t* in, int minutes, os_time_t* out);
int time_tz_offset(void);
void time_set_tz_offset(int minutes);
void time_format_tz(int minutes, char* out, int max);

#endif
