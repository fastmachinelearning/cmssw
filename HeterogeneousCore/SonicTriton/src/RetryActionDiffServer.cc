#include "HeterogeneousCore/SonicTriton/interface/RetryActionDiffServer.h"
#include "HeterogeneousCore/SonicTriton/interface/TritonClient.h"
#include "HeterogeneousCore/SonicTriton/interface/TritonService.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ServiceRegistry/interface/Service.h"

RetryActionDiffServer::RetryActionDiffServer(const edm::ParameterSet& conf, SonicClientBase* client)
    : RetryActionBase(conf, client) {}

void RetryActionDiffServer::start() { this->shouldRetry_ = true; }

void RetryActionDiffServer::retry() {
  if (!this->shouldRetry_) {
    this->shouldRetry_ = false;
    edm::LogInfo("RetryActionDiffServer") << "Retry not armed; skipping.";
    return;
  }

  try {
    auto* tritonClient = static_cast<TritonClient*>(client_);
    edm::LogInfo("RetryActionDiffServer") << "Attempting retry by switching to fallback server";
    // TODO: Get the server name from TritonService, use fallback for testing
    edm::Service<TritonService> ts;

    // get best server, ignoring the current server
    auto bestServerName = ts->getBestServer(tritonClient->modelName(),tritonClient->serverName());

    if (bestServerName) {
      tritonClient->updateServer(*bestServerName);
      eval();
    } else {
      edm::LogWarning("RetryActionDiffServer") 
          << "No alternative server found for model " << tritonClient->modelName();
    }
  } catch (TritonException& e) {
    e.convertToWarning();
  } catch (std::exception& e) {
    edm::LogError("RetryActionDiffServer") << "Failed to retry with alternative server: " << e.what();
  } catch (...) {
    edm::LogError("RetryActionDiffServer: UnknownFailure") << "An unknown exception was thrown";
  }
  this->shouldRetry_ = false;
}

DEFINE_RETRY_ACTION(RetryActionDiffServer);
