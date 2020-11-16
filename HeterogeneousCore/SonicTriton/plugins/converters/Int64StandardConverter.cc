#include "HeterogeneousCore/SonicTriton/interface/TritonConverterBase.h"

class Int64StandardConverter : public TritonConverterBase<int64_t> {
public:
  Int64StandardConverter(const edm::ParameterSet& conf) : TritonConverterBase<int64_t>(conf) {}

  const uint8_t* convertIn(const int64_t* in) override;
  const int64_t* convertOut(const uint8_t* in) override;
};

DEFINE_EDM_PLUGIN(TritonConverterFactory<int64_t>, Int64StandardConverter, "Int64StandardConverter");

const uint8_t* Int64StandardConverter::convertIn(const int64_t* in) { return reinterpret_cast<const uint8_t*>(in); }

const int64_t* Int64StandardConverter::convertOut(const uint8_t* in) { return reinterpret_cast<const int64_t*>(in); }
