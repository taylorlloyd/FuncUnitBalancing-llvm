#ifndef BALANCE_FUNCTIONAL_UNITS_H
#define BALANCE_FUNCTIONAL_UNITS_H

#include <cfloat>

using namespace std;

namespace llvm {

  class BalanceFunctionalUnits : public LoopPass {
    public:
      static char ID;

      BalanceFunctionalUnits() : LoopPass(ID) {}

      void getAnalysisUsage(AnalysisUsage &AU) const override;
      bool runOnLoop(Loop *L, LPPassManager &LPM) override;
    private:
      float overuseRateThreshold = FLT_MAX;
      BlockFrequencyInfo *BFI;

      pair<Transformation*, Instruction*> selectNextTransformation(Loop *L, array<unsigned long, FuncUnit::NumFuncUnits> usage);
      array<unsigned long, FuncUnit::NumFuncUnits> transformationEffect(Transformation *tsfm, Instruction *I, array<unsigned long, FuncUnit::NumFuncUnits> usage);
      float overuseRate(array<unsigned long, FuncUnit::NumFuncUnits> usage);
      vector<Transformation*> transformations = {new ShlToMul, new ShrToDiv};
  };

} // end namespace
#endif
