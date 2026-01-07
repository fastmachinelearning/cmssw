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
    edm::Service<TritonService> ts;

    // First, try to find another remote server
    auto bestServer = ts->getBestServer(tritonClient->modelName(), tritonClient->serverName());
    if (bestServer) {
      edm::LogInfo("RetryActionDiffServer") << "Attempting retry with alternative server: " << *bestServer;
      tritonClient->updateServer(*bestServer);
      eval();
    } else {
      // No remote server available, fall back to local fallback server with dynamic loading
      edm::LogInfo("RetryActionDiffServer") << "No alternative remote server available, trying fallback server";
      tritonClient->updateServer(TritonService::Server::fallbackName);

      // Load model on fallback (path is retrieved from models_ map)
      bool loaded = ts->loadModel(tritonClient->modelName());
      if (!loaded) {
        edm::LogWarning("RetryActionDiffServer") << "Fallback dynamic load failed for model "
                                                  << tritonClient->modelName();
        this->shouldRetry_ = false;
        return;
      }

      // Re-evaluate on fallback
      eval();
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
