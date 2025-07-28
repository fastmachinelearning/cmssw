#ifndef HeterogeneousCore_SonicTriton_RetryActionDiffServer_h
#define HeterogeneousCore_SonicTriton_RetryActionDiffServer_h

#include "HeterogeneousCore/SonicCore/interface/RetryActionBase.h"

class RetryActionDiffServer : public RetryActionBase {
public:
  RetryActionDiffServer(const edm::ParameterSet& conf, SonicClientBase* client);
  ~RetryActionDiffServer() override = default;

  void retry() override;
  void start() override;

private:
  std::string diff_server_url_;
  std::string diff_server_token_;
};

#endif

