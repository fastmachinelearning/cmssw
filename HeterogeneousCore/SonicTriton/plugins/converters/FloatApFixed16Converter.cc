#include "HeterogeneousCore/SonicTriton/interface/TritonConverterBase.h"

#include <string>
#include "ap_fixed.h"

template <int I>
class FloatApFixed16Converter : public TritonConverterBase<float> {
public:
  FloatApFixed16Converter() : TritonConverterBase<float>("FloatApFixed16F"+std::to_string(I)+"Converter", 2) {}

  const uint8_t* convertIn(const float* in) const {
    auto temp_vec = std::make_shared<std::vector<ap_fixed<16, I>>>(std::move(this->makeVecIn(in)));
    inputHolder_.push_back(temp_vec);
    return reinterpret_cast<const uint8_t*>(temp_vec->data());
  }
  const float* convertOut(const uint8_t* in) const {
    auto temp_vec = std::make_shared<std::vector<float>>(std::move(this->makeVecOut(reinterpret_cast<const ap_fixed<16, I>*>(in))));
    outputHolder_.push_back(temp_vec);
    return temp_vec->data();
  }

  void clear() const {
    inputHolder_.clear();
    outputHolder_.clear();
  }

private:
  std::vector<ap_fixed<16, I>> makeVecIn(const float* in) const {
    unsigned int nfeat = sizeof(in) / sizeof(float);
    std::vector<ap_fixed<16, I>> temp_storage(in, in + nfeat);
    return temp_storage;
  }

  std::vector<float> makeVecOut(const ap_fixed<16, I>* in) const {
    unsigned int nfeat = sizeof(in) / sizeof(ap_fixed<16, I>);
    std::vector<float> temp_storage(in, in + nfeat);
    return temp_storage;
  }

  mutable std::vector<std::shared_ptr<std::vector<ap_fixed<16, I>>>> inputHolder_;
  mutable std::vector<std::shared_ptr<std::vector<float>>> outputHolder_;
};

DEFINE_TRITON_CONVERTER(float, FloatApFixed16Converter<6>, "FloatApFixed16F6Converter");
