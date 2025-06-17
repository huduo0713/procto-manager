#include "xgboost_model.h"
#include <xgboost/c_api.h>        // XGBoost 的 C API 头文件
#include <iostream>
#include <vector>
#include <string>

XGBoostModel::XGBoostModel() : booster_(nullptr) {}

XGBoostModel::~XGBoostModel() {
    if (booster_) {
        XGBoosterFree(booster_);
    }
}

bool XGBoostModel::LoadModel(const std::string& model_path) {
    if (XGBoosterCreate(nullptr, 0, &booster_) != 0) {
        std::cerr << "Failed to create booster!" << std::endl;
        return false;
    }
    if (XGBoosterLoadModel(booster_, model_path.c_str()) != 0) {
        std::cerr << "Failed to load model from: " << model_path << std::endl;
        return false;
    }
    return true;
}

bool XGBoostModel::Predict(const std::vector<float>& input, int feature_num, std::vector<float>& result) const {
    if (!booster_) {
        std::cerr << "Booster not initialized!" << std::endl;
        return false;
    }

    DMatrixHandle dmat;
    if (XGDMatrixCreateFromMat(input.data(), 1, feature_num, -1, &dmat) != 0) {
        std::cerr << "Failed to create DMatrix!" << std::endl;
        return false;
    }

    bst_ulong out_len;
    const float* out_result;
    if (XGBoosterPredict(booster_, dmat, 0, 0, 0, &out_len, &out_result) != 0) {
        std::cerr << "Prediction failed!" << std::endl;
        XGDMatrixFree(dmat);
        return false;
    }

    result.assign(out_result, out_result + out_len);
    XGDMatrixFree(dmat);
    return true;
}