#include "HeterogeneousCore/SonicCore/interface/RetryActionBase.h"
#include "HeterogeneousCore/SonicCore/interface/SonicClientBase.h"

class RetrySameServerAction : public RetryActionBase {
public:
    RetrySameServerAction(const edm::ParameterSet& pset, SonicClientBase* client) 
        : RetryActionBase(pset, client),
          allowedTries_(pset.getUntrackedParameter<unsigned>("allowedTries", 0)) {}
protected:
    void retry();

private:
    unsigned allowedTries_,tries_;
};
