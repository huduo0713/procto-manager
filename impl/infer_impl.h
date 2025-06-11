#ifndef INFER_IMPL_H
#define INFER_IMPL_H

#ifdef __cplusplus

#include <iostream>
#include <assert.h>

// Infer.hpp
#pragma once
#include <string>
#include <cstdint>

/*******************默认配置********************************/ 
// 存储在共享内存里面的全局配置，默认为1行，存储在第0行
#define CONFIG_LEN 1

// 存储数据类型， 这里固定为float, 4字节
#define DATA_TYPE_LEN   4
/*******************默认配置********************************/ 


/*******************算法配置********************************/ 
// 数据长度, 根据实际的数据集进行修改，该值决定batch == DATA_LEN时， 执行infer方法
#define DATA_LEN  6

// 特征长度， 该值 > 2, 前两个值存储时间戳, 根据实际的数据集进行修改
#define FEAT_LEN   4

// 推理频率 推理时间=循环任务周期*FREQ 
#define FREQ    2
/*******************算法配置********************************/ 


struct AlgoOutput{
    float value;
};

enum CONFIG_TABLE{
    WRITE_INDEX,  // 记录数据写索引
    EXECUTE_COUNT,   // 记录执行次数，该值可以结合任务周期计算时间
    INFER_READY, //  记录推理是否准备好， 但WRITE_INDEX=DATA_LEN时，INFER_READY就已经准备好 
};

class Infer {
public:
    Infer(int feat_len);
    ~Infer();

    int write_data_to_memory();
    void pretty_print();
    int preprocess(long int ts, const int* data, int feat_len);
    int infer();
    int postprocess();
    int run(bool startup, long int ts, int *data, int feat_len, AlgoOutput* out);
    void update_execute_count();
    inline void set_index(CONFIG_TABLE index, float val){
        assert(shm_ptr_);
        float *const ptr = shm_ptr_;
        ptr[index] = val;
    }

    inline float get_index(CONFIG_TABLE index){
        assert(shm_ptr_);
        float *const ptr = shm_ptr_;
        return ptr[index];

    }

private:
    // 循环缓冲处理, index 范围：[1, N)
    inline void advance_index(int &index, int N) {
        index++;
        index = (index == N) ? 1 : index;
    }

private:
    void initSharedMemory(bool flag);

    int data_len_;
    int feat_len_;
    int data_type_len_;
    size_t TOTAL_SIZE;

    int write_index_ = 1;       // 当前写入位置, 从index 1开始，index 0 存储中间变量
    bool shm_initialized_;  // 是否初始化标记
    bool infer_ready_ = false; // 使能推理 
    int execute_count_ = 0;    // 执行次数

    float* shm_ptr_;        // 共享内存映射指针
    int shm_fd_;            // 共享内存文件描述符
    std::string shm_name_;
};


extern "C" {
#endif

int algo(bool startup, long int ts, int *data, int len, AlgoOutput* out);

#ifdef __cplusplus
}
#endif

#endif // INFER_IMPL_H

