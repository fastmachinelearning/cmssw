#include "HeterogeneousCore/SonicCore/interface/RetryActionBase.h"
#include "HeterogeneousCore/SonicCore/interface/SonicClientBase.h"

class RetrySameServerAction : public RetryActionBase {
public:
  RetrySameServerAction(const edm::ParameterSet& pset, SonicClientBase* client)
      : RetryActionBase(pset, client), allowedTries_(pset.getUntrackedParameter<unsigned>("allowedTries", 0)) {}

  void start() override { tries_ = 0; };

protected:
  void retry() override;

private:
  unsigned allowedTries_, tries_;
};
