#include "time_feature_utils.h"

// 计算特征在指定时间步长内的平均值
std::vector<float> compute_average_feature(std::vector<ParsedTime> time_series, std::vector<std::vector<float>> feature, std::optional<int> step){
    // 时间序列长度
    int time_size = time_series.size();
    // 获取实际使用的步长，如果未提供step，则使用整个时间序列长度
    int actual_step = step.has_value() ? step.value() : static_cast<int>(time_series.size());
    
    // 初始化每个特征的总和为0（假设特征维度为feature.size()）
    std::vector<float> sum(feature.size(), 0.0f);

    // 累加前actual_step个时间点的特征值
    for (size_t i = 1; i <= actual_step; i++)
    {
        for (size_t index = 0; index < sum.size(); index++)
        {
            // 注意：访问feature[index][i]可能越界（从1开始），调用时需确保数据合法
            sum[index] += feature[index][i-1];
        }
    }

    // 除以步长，计算平均值
    for (size_t index = 0; index < sum.size(); index++)
    {
        sum[index] /= actual_step;
    }
    
    return sum;
}

// 计算一年中的第几天
int getDayOfYear(const ParsedTime& timeInfo){
    // 检查月份是否合法（范围1~12）
    if (timeInfo.month < 1 || timeInfo.month > 12) {
        log_error("Invalid month: {} (must be 1~12)", timeInfo.month);
        return -1;
    }

    // 每个月的天数（索引0未用，只是为了方便使用month作为索引）
    const int monthDays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // 默认当前月最大天数
    int maxDay = monthDays[timeInfo.month];
    
    // 判断是否为闰年（仅2月有影响）
    bool isLeapYear = false;
    if (timeInfo.month == 2) {
        isLeapYear = (timeInfo.year % 400 == 0) || 
                     (timeInfo.year % 100 != 0 && timeInfo.year % 4 == 0);
        if (isLeapYear) {
            maxDay = 29;  // 闰年2月29天
        }
    }

    // 检查日期是否合法
    if (timeInfo.day < 1 || timeInfo.day > maxDay) {
        log_error("Invalid day: {} for month {} (max: {})", timeInfo.day, timeInfo.month, maxDay);
        return -1;
    }

    // 计算从1月1日到当前日期的累计天数
    int dayOfYear = timeInfo.day;
    for (int m = 1; m < timeInfo.month; ++m) {
        dayOfYear += monthDays[m];
    }

    // 如果是闰年且月份大于2，加1天
    if (timeInfo.month > 2 && isLeapYear) {
        dayOfYear += 1;
    }

    return dayOfYear;
}

// 计算当前时间是当天的第几分钟
int getMinuteOfDay(const ParsedTime& timeInfo){
    // 小时合法性校验
    if (timeInfo.hour < 0 || timeInfo.hour > 23) {
        log_error("Invalid hour: {} (must be 0-23)", timeInfo.hour);
        return -1;
    }

    // 分钟合法性校验
    if (timeInfo.minute < 0 || timeInfo.minute > 59) {
        log_error("Invalid minute: {} (must be 0-59)", timeInfo.minute);
        return -1;
    }

    // 转换为总分钟数
    int minuteOfDay = timeInfo.hour * 60 + timeInfo.minute;
    
    return minuteOfDay;
}

// 判断某日期是星期几（返回Zeller公式结果：0=周六，1=周日，2=周一...6=周五）
int isWeekend(const ParsedTime& timeInfo) {
    // 获取年月日
    int year = timeInfo.year;
    int month = timeInfo.month;
    int day = timeInfo.day;

    // Zeller公式需要将1月2月视为上一年的13月14月
    if (month < 3) {
        month += 12;
        year -= 1;
    }

    int century = year / 100;
    year = year % 100;

    // Zeller公式计算星期几
    int weekday = (day + 13*(month+1)/5 + year + year/4 + century/4 + 5*century) % 7;

    return weekday;  // 返回的weekday可用于判断是否是周末
}
