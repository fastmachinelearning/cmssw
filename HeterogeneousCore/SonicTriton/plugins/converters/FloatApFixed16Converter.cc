#include "HeterogeneousCore/SonicTriton/interface/TritonConverterBase.h"

#include "ap_fixed.h"

template <int I>
class FloatApFixed16Converter : public TritonConverterBase<float> {
public:
  FloatApFixed16Converter(const edm::ParameterSet& conf) : TritonConverterBase<float>(conf, 2) {}

  const uint8_t* convertIn(const float* in) override;
  const float* convertOut(const uint8_t* in) override;

private:
  std::vector<ap_fixed<16, I>> makeVecIn(const float* in) {
    unsigned int nfeat = sizeof(in) / sizeof(float);
    std::vector<ap_fixed<16, I>> temp_storage(in, in + nfeat);
    return temp_storage;
  }

  std::vector<float> makeVecOut(const ap_fixed<16, I>* in) {
    unsigned int nfeat = sizeof(in) / sizeof(ap_fixed<16, I>);
    std::vector<float> temp_storage(in, in + nfeat);
    return temp_storage;
  }
};

DEFINE_EDM_PLUGIN(TritonConverterFactory<float>, FloatApFixed16Converter<6>, "FloatApFixed16F6Converter");

template <int I>
const uint8_t* FloatApFixed16Converter<I>::convertIn(const float* in) {
  return reinterpret_cast<const uint8_t*>((this->makeVecIn(in)).data());
}

template <int I>
const float* FloatApFixed16Converter<I>::convertOut(const uint8_t* in) {
  return (this->makeVecOut(reinterpret_cast<const ap_fixed<16, I>*>(in))).data();
}
