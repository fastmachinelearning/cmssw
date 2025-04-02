#include "HeterogeneousCore/SonicCore/interface/RetrySameServerAction.h"
#include "HeterogeneousCore/SonicCore/interface/SonicClientBase.h"

void RetrySameServerAction::retry() {
  ++tries_;
  //if max retries has not been exceeded, call evaluate again
  if (tries_ < allowedTries_) {
    eval();
    return;
  }else{
     shouldRetry_ = false;  // Flip flag when max retries are reached
     edm::LogInfo("RetrySameServerAction") << "Max retry attempts reached. No further retries.";
  }
}

DEFINE_EDM_PLUGIN(RetryActionFactory, RetrySameServerAction, "RetrySameServerAction");
