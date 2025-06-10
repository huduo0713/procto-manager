#include <stdio.h>
#include <fstream>
#include <assert.h>

#include "infer_impl.h"
#include "one_logger.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

void Infer::pretty_print(){
    printf("Matrix [data_len_=%d, feat_len=%d]:\n", data_len_ + CONFIG_LEN, feat_len_);
    for (int i = 0; i < data_len_ + CONFIG_LEN; ++i) {
        // 配置项
        if (i == 0){
            printf("Row %02d [c] ", i);
        // 指向当前写的index
        }else if (i == write_index_) {
            printf("Row %02d --> ", i);
        } else {
            printf("Row %02d     ", i);
        }

        for (int j = 0; j < feat_len_; ++j) {
            printf("%16.3f ", shm_ptr_[i * feat_len_ + j]);
        }
        printf("\n");
    }
    return;
}

Infer::Infer(int feat_len)
    : data_len_(DATA_LEN), feat_len_(feat_len), data_type_len_(DATA_TYPE_LEN),
      write_index_(CONFIG_LEN), shm_initialized_(false), shm_ptr_(nullptr), shm_fd_(-1)
{
    // 多申请CONFIG行，用于存储中间变量，第一行第一个存write_index, 其他暂时保留
    TOTAL_SIZE = (data_len_ + CONFIG_LEN) * feat_len_ * data_type_len_;
    shm_name_ = "algo_sharemem";
}

Infer::~Infer() {
    if (shm_ptr_) {
        munmap(shm_ptr_, TOTAL_SIZE);
    }
    if (shm_fd_ >= 0) {
        // 不关闭内存
        // close(shm_fd_);
        // shm_unlink(shm_name_.c_str());
    }
}

void Infer::initSharedMemory(bool flag) {
    if (shm_initialized_) return;

    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd_ < 0) {
        log_error("shm_open failed");
        return;
    }

    if (ftruncate(shm_fd_, TOTAL_SIZE) != 0) {
        log_error("ftruncate failed");
        return;
    }

    //   长度是字节长度
    shm_ptr_ = static_cast<float*>(mmap(NULL, TOTAL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0));
    if (shm_ptr_ == MAP_FAILED) {
        log_error("mmap failed");
        return;
    }

    // 首次清空内存, 仅flag为true的时候清空内存为0
    if (flag){
        log_info("cleaning the shm_ptr at the first time.");
        memset(shm_ptr_, 0, TOTAL_SIZE);
        // 写中间变量指针
        // 存储write_index到 第一列第1个地址
        set_index(write_index_);
    }
    shm_initialized_ = true;
}

/**
 * 共享内存格式定义为一个二维数组 Matrix[y+1,x+2]，其中
 * y = batch长度(多少帧)，也就是data_len; 
 * x = 一帧数据的特征个数，也就是feat_len； 
 * Row 0:  write_index | reserved  | resv_1   | resv_2   | resv_3 ... | resv_x
 * Row 1:  t_stamp_H   | t_stamp_L | feat_1   | feat_2   | feat_3 ... | feat_x
 * ...
 * Row y:  t_stamp_H   | t_stamp_L | feat_1   | feat_2   | feat_3 ... | feat_x
 * 
 * 
*/
int Infer::preprocess(long int ts, const int* data, int feat_len) {
    // feat_len 包含 2 个时间戳 + data的长度
    int frame_size = feat_len;

    // 确认拿到的数据是否等于算法的配置长度
    if (FEAT_LEN != frame_size) {
        log_error("Input data length does not match expected frame size: FEAT_LEN = {}, but data frame_size = {}", FEAT_LEN, frame_size);
        return 1;
    }

    // 从缓存区读取writet_index_
    write_index_ = get_index();
    // 写数据指针
    float* const base_ptr = shm_ptr_ + write_index_ * frame_size;

    if (frame_size > 0) {
        // 先写时间戳：将时间戳放到最前面的两个数
        // high
        base_ptr[0] = static_cast<float>(ts >> 32);
        // low
        base_ptr[1]  = static_cast<float>(ts & 0xFFFFFFFF);

         // 再写入特征数据：（从 index 2 开始）, 长度要减去时间戳的长度2
        for (int i = 0; i < frame_size - 2; ++i) {
            base_ptr[2 + i] = static_cast<float>(data[i]);
        }
        // 打印内存结构
        pretty_print();
    }

    // 环形写索引更新, 从 index 1~DATA_LEN循环, 注意不是从0开始，因为index 0 存储了中间变量
    write_index_ = (write_index_ + 1) % (DATA_LEN + CONFIG_LEN);
    // index写回到缓存
    set_index(write_index_);

    return 0;
}

int Infer::infer() {
    if (!shm_initialized_ || !shm_ptr_) return 2;


    // 假设处理所有 DATA 数据, 从index 1开始
    for (int i = CONFIG_LEN; i < DATA_LEN; ++i) {
        // 每一行的指针， 长度为FEAT_LEN
        float* frame = shm_ptr_ + i * FEAT_LEN;
    }

    return 0;
}

int Infer::run(bool startup, long int ts, int *data, int feat_len, AlgoOutput* out){
    int ret = 0;
    // init shared memory
    initSharedMemory(startup);
    // preprocess
    ret = preprocess(ts, data, feat_len);
    if (ret){
        log_error("preprocess error.");
        return ret;
    }
    // infer
    ret = infer();
    if (ret){
        log_error("infer error.");
        return ret;
    }



}

int Infer::postprocess() {
    log_info("[Postprocess] Done");
}

extern "C" int algo(bool startup, long int ts, int *data, int len, AlgoOutput* out){
    Infer infer(len);
    int ret = infer.run(startup, ts, data, len, out);
    if (ret){
        log_error("err code = {}", ret);
    }
    return ret;


}