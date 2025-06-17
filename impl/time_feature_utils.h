#ifndef TIME_FEATURE_UTILS_H
#define TIME_FEATURE_UTILS_H

#include "parsed_time.h"
#include "one_logger.hpp"
#include <vector>
#include <optional>

/**
 * @brief 计算时间序列与特征之间的平均特征量（或特定步长下的平均特征量）
 * 
 * @param time_series 时间序列数据，每个时间点的结构为 ParsedTime。
 * @param feature     与时间点对应的特征二维数组（每行对应一个时间点的特征）。
 * @param step        可选的步长参数，若提供，则按步长分段取平均；否则处理全部数据。
 * 
 * @return float      返回计算得到的各个特征的平均特征量。 
 */
std::vector<float> compute_average_feature(std::vector<ParsedTime> time_series,
                           std::vector<std::vector<float>> feature,
                           std::optional<int> step = std::nullopt);

/**
 * @brief 计算给定日期是一年中的第几天（1~365/366）
 * @param timeInfo 输入的时间结构体，至少需要包含有效的 year, month, day
 * @return int 返回该日期在一年中的第几天（1月1日 = 1，12月31日 = 365或366）
 */
int getDayOfYear(const ParsedTime& timeInfo);

/**
 * @brief 计算当前时间是一天中的第多少分钟（0-1439）
 * @param timeInfo 包含时、分的ParsedTime结构体
 * @return int 当天分钟数（0-1439）
 */
int getMinuteOfDay(const ParsedTime& timeInfo);

/**
 * @brief 判断给定日期是否是周末（周六或周日）
 * @param timeInfo 包含年、月、日的ParsedTime结构体
 * @return int 0代表周六，1代表周日
 */
int isWeekend(const ParsedTime& timeInfo);

#endif // TIME_FEATURE_UTILS_H