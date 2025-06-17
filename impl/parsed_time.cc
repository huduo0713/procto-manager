#include "parsed_time.h"
#include <iostream>  // 添加头文件用于输出

ParsedTime timestamp_to_time(float high_part, float low_part) {
    uint64_t high = static_cast<uint64_t>(high_part);
    uint64_t low = static_cast<uint64_t>(low_part);
    uint64_t ts = (high << 32) | low;

    std::time_t raw_time = static_cast<std::time_t>(ts / 1000);  // 毫秒转秒
    std::tm* timeinfo = std::localtime(&raw_time);

    ParsedTime t;
    t.year = 1900 + timeinfo->tm_year;
    t.month = 1 + timeinfo->tm_mon;
    t.day = timeinfo->tm_mday;
    t.hour = timeinfo->tm_hour;
    t.minute = timeinfo->tm_min;
    t.second = timeinfo->tm_sec;
    t.granlarity = ParsedTime::Granularity::SECOND;

    return t;
}
