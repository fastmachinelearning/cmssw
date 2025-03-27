#include "HeterogeneousCore/SonicCore/interface/RetrySameServerAction.h"
#include "HeterogeneousCore/SonicCore/interface/SonicClientBase.h"

void RetrySameServerAction::retry()  {
     ++tries_;
    //if max retries has not been exceeded, call evaluate again
    if (tries_ < allowedTries_) {
      eval();
      return;
    }
}
