#include "HeterogeneousCore/SonicTriton/interface/TritonConverterBase.h"

class FloatStandardConverter : public TritonConverterBase<float> {
public:
  FloatStandardConverter() : TritonConverterBase<float>("FloatStandardConverter") {}

  const uint8_t* convertIn(const float* in) const { return reinterpret_cast<const uint8_t*>(in); }
  const float* convertOut(const uint8_t* in) const { return reinterpret_cast<const float*>(in); }
};

DEFINE_TRITON_CONVERTER_SIMPLE(float, FloatStandardConverter);
