#ifndef RETRY_ACTION_BASE_H
#define RETRY_ACTION_BASE_H

#include "FWCore/PluginManager/interface/PluginFactory.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "HeterogeneousCore/SonicCore/interface/SonicClientBase.h"
#include <memory>
#include <string>

// Base class for retry actions
class RetryActionBase {
public:
    RetryActionBase(const edm::ParameterSet& conf, SonicClientBase* client);
    virtual ~RetryActionBase() = default;
protected:
    virtual void retry() = 0; // Pure virtual function for execution logic
    void eval(); // interface for calling evaluate in client
 
protected:
    SonicClientBase* client_;
};

// Define the factory for creating retry actions
using RetryActionFactory = edmplugin::PluginFactory<RetryActionBase*(const edm::ParameterSet&, SonicClientBase* client)>;

#endif // RETRY_ACTION_BASE_H
