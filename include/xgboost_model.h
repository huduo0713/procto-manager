#ifndef XGBOOST_MODEL_H
#define XGBOOST_MODEL_H

#include <xgboost/c_api.h>
#include <vector>
#include <string>

class XGBoostModel {
public:
    XGBoostModel();
    ~XGBoostModel();

    bool LoadModel(const std::string& model_path);
    bool Predict(const std::vector<float>& input, int feature_num, std::vector<float>& result) const;

private:
    BoosterHandle booster_;
};

#endif  // XGBOOST_MODEL_H
