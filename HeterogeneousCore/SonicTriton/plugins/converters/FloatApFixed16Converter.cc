#include "HeterogeneousCore/SonicTriton/interface/TritonConverterBase.h"

#include <string>
#include "ap_fixed.h"

template <int I>
class FloatApFixed16Converter : public TritonConverterBase<float> {
public:
  FloatApFixed16Converter() : TritonConverterBase<float>("FloatApFixed16F"+std::to_string(I)+"Converter", 2) {}

  const uint8_t* convertIn(const float* in) const {
    return reinterpret_cast<const uint8_t*>((this->makeVecIn(in)).data());
  }
  const float* convertOut(const uint8_t* in) const {
    return (this->makeVecOut(reinterpret_cast<const ap_fixed<16, I>*>(in))).data();
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
};

DEFINE_TRITON_CONVERTER(float, FloatApFixed16Converter<6>, "FloatApFixed16F6Converter");
