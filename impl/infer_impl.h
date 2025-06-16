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

// 存储数据类型， 这里modbux协议类型的寄存器长度2字节
#define DATA_TYPE_LEN   2
/*******************默认配置********************************/ 


/*******************算法配置********************************/ 
// 数据个数长度, 根据实际的数据集进行修改，该值决定batch == DATA_LEN时， 执行infer方法
#define DATA_LEN  6

// 特征长度， 根据实际的数据集进行修改
#define FEAT_LEN  2 
// 推理频率 推理时间=循环任务周期*FREQ 
#define FREQ    2
/*******************算法配置********************************/ 

#pragma pack(push, 1)
struct DataTable {
    uint64_t timestamp_ms;
    uint16_t data[FEAT_LEN];  // 指定长度
};

struct ConfigTable{
    uint16_t write_index;
    uint16_t infer_ready;
    uint32_t execute_count;
};
#pragma pack(pop)

enum DataType{
    DINT8 = 1,
    DINT16 = 2,
    DINT32 = 4,
    DINT64 = 8,
};

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

    int write_config_to_memory();
    void pretty_print();
    int preprocess(uint64_t ts, uint8_t *data, int len, DataType type);
    int infer();
    int postprocess();
    int run(bool startup, uint64_t ts, uint8_t *data, int len, DataType type, AlgoOutput* out);
    void update_execute_count();
    inline void set_index(CONFIG_TABLE index, float val){
        assert(shm_ptr_);
        ConfigTable *const ptr = (ConfigTable*)shm_ptr_;
        if (index == CONFIG_TABLE::EXECUTE_COUNT){
            ptr->execute_count = val;
        }else if (index == CONFIG_TABLE::INFER_READY){
            ptr->infer_ready = val;
        }else{
            ptr->write_index = val;
        }
    }

    inline ConfigTable get_index(CONFIG_TABLE index){
        assert(shm_ptr_);
        const ConfigTable *ptr = (ConfigTable*)shm_ptr_;
        return *ptr;

    }

private:
    // 循环缓冲处理, index 范围：[1, N)
    inline void advance_index(int &index, int N) {
        index++;
        // 写入内存
        index = (index == N) ? 1 : index;
        set_index(CONFIG_TABLE::WRITE_INDEX, index);
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

    uint8_t* shm_ptr_;        // 共享内存映射指针
    int shm_fd_;            // 共享内存文件描述符
    std::string shm_name_;
};


extern "C" {
#endif

/**
 * @brief 算法与PLC交互的接口
 * startup: PLC主控进程首次启动， 用于内存管理
 * ts: 时间戳 data数据的时间戳, 单位毫秒级
 * data: 数据指针，统一使用字节类型存储
 * len: data 数据长度, 也就是数据特征值个数
 * type: 数据类型，数据类型，PLC和算法接口约定。数据类型决定每个特征值占用内存空间的大小, 如果type = int, 则每个特征值占用4字节空间
 * out: 算法给出结果后的输出数据，目前只输出一个float类型的结果out->value;
 * 
 * return 0: 正常运行, 未触发推理； 
 *        1: 正常运行，触发了推理计算；
 *         其他：异常
*/
int algo(bool startup, uint64_t ts, uint8_t *data, int len, DataType type, AlgoOutput* out);

#ifdef __cplusplus
}
#endif

#endif // INFER_IMPL_H

