#ifndef HeterogeneousCore_SonicTriton_TritonConverterBase
#define HeterogeneousCore_SonicTriton_TritonConverterBase

#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DataFormats/Common/interface/Handle.h"

#include <string>

template <typename DT>
class TritonConverterBase {
//class needs to be templated since the convert functions require the data type, but need to also be virtual, and virtual member function templates are not allowed in C++
public:
  TritonConverterBase(const std::string convName)
      : converterName_(convName), byteSize_(sizeof(DT)) {}
  TritonConverterBase(const std::string convName, size_t byteSize)
      : converterName_(convName), byteSize_(byteSize) {}
  TritonConverterBase(const TritonConverterBase&) = delete;
  virtual ~TritonConverterBase() = default;
  TritonConverterBase& operator=(const TritonConverterBase&) = delete;

  virtual const uint8_t* convertIn (const DT* in) const = 0;
  virtual const DT* convertOut (const uint8_t* in) const = 0;

  const int64_t byteSize() const { return byteSize_; }

  const std::string& name() const { return converterName_; }

  virtual void clear() const {}

private:
  const std::string converterName_;
  const int64_t byteSize_;
};

#include "FWCore/PluginManager/interface/PluginFactory.h"

template <typename DT>
using TritonConverterFactory = edmplugin::PluginFactory<TritonConverterBase<DT>*()>;

#define DEFINE_TRITON_CONVERTER(input, type, name) DEFINE_EDM_PLUGIN(TritonConverterFactory<input>, type, name)
#define DEFINE_TRITON_CONVERTER_SIMPLE(input, type) DEFINE_EDM_PLUGIN(TritonConverterFactory<input>, type, #type)

#endif
