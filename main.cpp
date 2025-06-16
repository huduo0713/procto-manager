#include "infer_impl.h"
#include <iostream>
#include <vector>
#include <cstdlib>  // for rand()
#include <ctime>    // for time()
#include <numeric>  // for accumulate()
#include <chrono>

#include <iostream>
#include <chrono>
#include "infer_impl.h"
#include "one_logger.hpp"


// 获取当前毫秒时间戳
long int get_timestamp_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int main() {
    // 输出定义
    AlgoOutput out;

    // 模拟特征长度, 这个特征值需要和算法的特征长度一致
    int feat_len = 2;

    // 模拟采集的数据
    uint16_t data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    // 处理data得到data_len的实际长度
    int data_len = (sizeof(data) / sizeof(data[0])) / feat_len;
    log_info("data_len = {}", data_len);
    // 每一帧循环
    for (int i = 0; i < data_len; ++i) {
        // 获取时间戳
        long int ts = get_timestamp_ms();
        // log_info("ts = {}", ts);
        /**********输入***********/
        // i == 0: 模拟第一次运行清空内存；
        // ts： 传递时间戳
        // &data[i * (feat_len - 2)]: 每一帧采集数据的首地址

        /**********输出***********/
        // &out: 算法结果写入到out指向的地址中
        if(!algo(i == 0, ts, reinterpret_cast<uint8_t*>(data + i * feat_len), feat_len, DataType::DINT16, &out)){
            log_info("the predict out = {}", out.value);
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));  // 等待 1 秒
    }



    return 0;
    
}
