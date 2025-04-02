#include "HeterogeneousCore/SonicCore/interface/SonicClientBase.h"
#include "HeterogeneousCore/SonicCore/interface/RetryActionBase.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/ParameterSet/interface/allowedValues.h"


// Custom deleter implementation
void SonicClientBase::RetryDeleter::operator()(RetryActionBase* ptr) const {
    delete ptr;
}

SonicClientBase::SonicClientBase(const edm::ParameterSet& params,
                                 const std::string& debugName,
                                 const std::string& clientName)
    : allowedTries_(params.getUntrackedParameter<unsigned>("allowedTries", 0)),
      debugName_(debugName),
      clientName_(clientName),
      fullDebugName_(debugName_) {
  if (!clientName_.empty())
    fullDebugName_ += ":" + clientName_;

  std::vector<edm::ParameterSet> retryPSetList = params.getParameter<std::vector<edm::ParameterSet>>("Retry");

  for (const auto& retryPSet : retryPSetList) {
        std::string actionType = retryPSet.getParameter<std::string>("retryType");

        auto retryAction = RetryActionFactory::get()->create(actionType, retryPSet, this);
        if (retryAction) {
            //Convert to  RetryActionPtr Type from raw pointer of retryAction 
            retryActions_.emplace_back(RetryActionPtr(retryAction.release()));
        }
  }

  std::string modeName(params.getParameter<std::string>("mode"));
  if (modeName == "Sync")
    setMode(SonicMode::Sync);
  else if (modeName == "Async")
    setMode(SonicMode::Async);
  else if (modeName == "PseudoAsync")
    setMode(SonicMode::PseudoAsync);
  else
    throw cms::Exception("Configuration") << "Unknown mode for SonicClient: " << modeName;
}

void SonicClientBase::setMode(SonicMode mode) {
  if (dispatcher_ and mode_ == mode)
    return;
  mode_ = mode;

  //get correct dispatcher for mode
  if (mode_ == SonicMode::Sync or mode_ == SonicMode::Async)
    dispatcher_ = std::make_unique<SonicDispatcher>(this);
  else if (mode_ == SonicMode::PseudoAsync)
    dispatcher_ = std::make_unique<SonicDispatcherPseudoAsync>(this);
}

void SonicClientBase::start(edm::WaitingTaskWithArenaHolder holder) {
  start();
  holder_ = std::move(holder);
}

void SonicClientBase::start() { 
    tries_ = 0; 
    // initialize all actions
    for (const auto& action : retryActions_) {
        action->start();
    }
}

void SonicClientBase::finish(bool success, std::exception_ptr eptr) {
  //retries are only allowed if no exception was raised
  if (!success and !eptr) {
    //++tries_;
    ////if max retries has not been exceeded, call evaluate again
    //if (tries_ < allowedTries_) {
    //  evaluate();
    //  //avoid calling doneWaiting() twice
    //  return;
    //}

    // Check if any retry actions are still valid
    bool anyRetryAllowed = false;
    for (const auto& action : retryActions_) {
        if (action->shouldRetry()) {
            action->retry();  // Call retry only if shouldRetry_ is true
            return;
        }
    }
    // If no actions allow retries, stop retrying
    if (!anyRetryAllowed) {
        edm::LogInfo("SonicClientBase") << "No retry actions available. Stopping retries.";
        return;
    }
    //prepare an exception if no more retries left
    else {
      edm::Exception ex(edm::errors::ExternalFailure);
      ex << "SonicCallFailed: call failed after max " << tries_ << " tries";
      eptr = make_exception_ptr(ex);
    }
  }
  if (holder_) {
    holder_->doneWaiting(eptr);
    holder_.reset();
  } else if (eptr)
    std::rethrow_exception(eptr);

  //reset client data now (usually done at end of produce())
  if (eptr)
    reset();
}

void SonicClientBase::fillBasePSetDescription(edm::ParameterSetDescription& desc, bool allowRetry) {
  //restrict allowed values
  desc.ifValue(edm::ParameterDescription<std::string>("mode", "PseudoAsync", true),
               edm::allowedValues<std::string>("Sync", "Async", "PseudoAsync"));
  if (allowRetry)
    desc.addUntracked<unsigned>("allowedTries", 0);
  desc.addUntracked<bool>("verbose", false);
}
