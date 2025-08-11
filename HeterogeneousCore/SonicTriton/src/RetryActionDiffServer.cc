#include "HeterogeneousCore/SonicTriton/interface/RetryActionDiffServer.h"
#include "HeterogeneousCore/SonicTriton/interface/TritonClient.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

RetryActionDiffServer::RetryActionDiffServer(
  const edm::ParameterSet& conf, 
  SonicClientBase* client
): RetryActionBase(conf, client) {
    alt_server_url_ = conf.getUntrackedParameter<std::string>("altServerUrl", "");
    alt_server_token_ = conf.getUntrackedParameter<std::string>("altServerToken", "");

    if (this->alt_server_url_.empty()) {
      edm::LogWarning("RetryActionDiffServer") 
        << "No alternative server URL provided. "
        << "This retry action will be disabled.";
      this->shouldRetry_ = false;
    }
}

void RetryActionDiffServer::start() {
  this->shouldRetry_ = true;
}

void RetryActionDiffServer::retry() {
  if (!this->shouldRetry_ || this->alt_server_url_.empty()) {
    this->shouldRetry_ = false;
    edm::LogInfo("RetryActionDiffServer") << "No alternative server available for retry.";
    return;
  }

  try {
    TritonClient* tritonClient = static_cast<TritonClient*>(client_);
    edm::LogInfo("RetryActionDiffServer") 
      << "Attempting retry by switching to server: " 
      << this->alt_server_url_;
    tritonClient->connectToServer(this->alt_server_url_);
    eval();
  } catch (const std::exception& e) {
    edm::LogError("RetryActionDiffServer") 
      << "Failed to retry with alternative server: "
      << e.what();
  }
  this->shouldRetry_ = false;
}

DEFINE_RETRY_ACTION(RetryActionDiffServer);