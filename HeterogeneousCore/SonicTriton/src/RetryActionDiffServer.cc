#include "HeterogeneousCore/SonicTriton/interface/RetryActionDiffServer.h"
#include "HeterogeneousCore/SonicTriton/interface/TritonClient.h"
#include "HeterogeneousCore/SonicTriton/interface/TritonService.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ServiceRegistry/interface/Service.h"

RetryActionDiffServer::RetryActionDiffServer(
  const edm::ParameterSet& conf, 
  SonicClientBase* client
): RetryActionBase(conf, client) {}

void RetryActionDiffServer::start() {
  this->shouldRetry_ = true;
}

void RetryActionDiffServer::retry() {
  if (!this->shouldRetry_) {
    this->shouldRetry_ = false;
    edm::LogInfo("RetryActionDiffServer") << "Retry not armed; skipping.";
    return;
  }

  try {
    auto* tritonClient = static_cast<TritonClient*>(client_);
    edm::LogInfo("RetryActionDiffServer") << "Attempting retry by switching to fallback server";
    tritonClient->updateServer(TritonService::Server::fallbackName);
    eval();
  } catch (const std::exception& e) {
    edm::LogError("RetryActionDiffServer") 
      << "Failed to retry with alternative server: "
      << e.what();
  }
  this->shouldRetry_ = false;
}

DEFINE_RETRY_ACTION(RetryActionDiffServer);