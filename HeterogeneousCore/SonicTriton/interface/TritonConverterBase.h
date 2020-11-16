#ifndef __TritonConverterBase_H__
#define __TritonConverterBase_H__

#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DataFormats/Common/interface/Handle.h"

#include <string>

template <typename DT>
class TritonConverterBase {
public:
  TritonConverterBase(const edm::ParameterSet& conf) : converterName_(conf.getParameter<std::string>("converterName")), byteSize_(sizeof(DT)) {}
  TritonConverterBase(const edm::ParameterSet& conf, size_t byteSize) : converterName_(conf.getParameter<std::string>("converterName")), byteSize_(byteSize) {}
  TritonConverterBase(const TritonConverterBase&) = delete;
  virtual ~TritonConverterBase() = default;
  TritonConverterBase& operator=(const TritonConverterBase&) = delete;

  virtual const uint8_t* convertIn(const DT* in) = 0;
  virtual const DT* convertOut(const uint8_t* in) = 0;

  virtual const int64_t getByteSize() const { return byteSize_; }

  const std::string& name() const { return converterName_; }

private:
  const std::string converterName_;
  const int64_t byteSize_;
};

#include "FWCore/PluginManager/interface/PluginFactory.h"

template <typename DT>
using TritonConverterFactory = edmplugin::PluginFactory<TritonConverterBase<DT>*(const edm::ParameterSet&)>;

#endif
