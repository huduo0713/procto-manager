#ifndef PARSED_TIME_H
#define PARSED_TIME_H

#include <ctime>
#include <cstdint>

struct ParsedTime {
    enum class Granularity{
        YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, UNKNOWN
    };
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    Granularity granlarity;     // 标识当前时间体的最小时间粒度
};

// 将 float 高/低位还原的时间戳转为结构体时间
ParsedTime timestamp_to_time(uint64_t timestamp);

#endif // PARSED_TIME_H