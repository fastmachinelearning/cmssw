#include "HeterogeneousCore/SonicTriton/interface/TritonConverterBase.h"

class FloatStandardConverter : public TritonConverterBase<float> {
public:
  FloatStandardConverter(const edm::ParameterSet& conf)
      : TritonConverterBase<float>(conf) {}

  const uint8_t* convertIn(const float* in) override;
  const float* convertOut(const uint8_t* in) override;
};

DEFINE_EDM_PLUGIN(TritonConverterFactory<float>, FloatStandardConverter, "FloatStandardConverter");

const uint8_t* FloatStandardConverter::convertIn(const float *in) {
    return reinterpret_cast<const uint8_t*>(in);
}

const float* FloatStandardConverter::convertOut(const uint8_t* in) {
    return reinterpret_cast<const float*>(in);
}
