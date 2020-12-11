#include "HeterogeneousCore/SonicTriton/interface/TritonConverterBase.h"

class Int64StandardConverter : public TritonConverterBase<int64_t> {
public:
  Int64StandardConverter() : TritonConverterBase<int64_t>("Int64StandardConverter") {}

  const uint8_t* convertIn(const int64_t* in) const override { return reinterpret_cast<const uint8_t*>(in); }
  const int64_t* convertOut(const uint8_t* in) const override { return reinterpret_cast<const int64_t*>(in); }
};

DEFINE_TRITON_CONVERTER_SIMPLE(int64_t, Int64StandardConverter);
