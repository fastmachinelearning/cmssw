#ifndef HeterogeneousCore_SonicTriton_RetryActionDiffServer_h
#define HeterogeneousCore_SonicTriton_RetryActionDiffServer_h

#include "HeterogeneousCore/SonicCore/interface/RetryActionBase.h"

/**
 * @class RetryActionDiffServer
 * @brief A concrete implementation of RetryActionBase that attempts to retry an inference
 * request on a different, user-specified Triton server.
 *
 * This class is designed to provide a fallback mechanism. If an initial inference
 * request fails (e.g., due to server unavailability or a model-specific error),
 * this action will be triggered. It reads an alternative server URL from the
 * ParameterSet and instructs the TritonClient to reconnect to this new server
 * for the retry attempt. This action is designed for one-time use per inference
 * call; after the retry attempt, it disables itself until the next `start()` call.
 */
 
class RetryActionDiffServer : public RetryActionBase {
public:
  RetryActionDiffServer(const edm::ParameterSet& conf, SonicClientBase* client);
  ~RetryActionDiffServer() override = default;

  void retry() override;
  void start() override;

private:
  std::string alt_server_url_;
  std::string alt_server_token_;
}; 

#endif

