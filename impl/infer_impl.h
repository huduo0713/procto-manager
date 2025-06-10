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
#define DATA_LEN  60

// 特征长度， 该值 > 2, 前两个值存储时间戳, 根据实际的数据集进行修改
#define FEAT_LEN   6
/*******************算法配置********************************/ 


struct AlgoOutput{
    float value;
};

class Infer {
public:
    Infer(int feat_len);
    ~Infer();

    void pretty_print();
    int preprocess(long int ts, const int* data, int feat_len);
    int infer();
    int postprocess();
    int run(bool startup, long int ts, int *data, int feat_len, AlgoOutput* out);
    inline void set_index(float val){
        assert(shm_ptr_);
        float *const ptr = shm_ptr_;
        ptr[0] = val;
    }

    inline float get_index(){
        assert(shm_ptr_);
        float *const ptr = shm_ptr_;
        return ptr[0];

    }

private:
    void initSharedMemory(bool flag);

    int data_len_;
    int feat_len_;
    int data_type_len_;
    size_t TOTAL_SIZE;

    int write_index_ = 1;       // 当前写入位置, 从index 1开始，index 0 存储中间变量
    bool shm_initialized_;  // 是否初始化标记

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

