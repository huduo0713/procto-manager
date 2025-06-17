#include "parsed_time.h"
#include <iostream>  // 添加头文件用于输出

ParsedTime timestamp_to_time(uint64_t timestamp) {
    std::time_t raw_time = static_cast<std::time_t>(timestamp / 1000);  // 毫秒转秒
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
