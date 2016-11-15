#ifndef BALANCE_FUNCTIONAL_UNITS_H
#define BALANCE_FUNCTIONAL_UNITS_H

#include <cfloat>

using namespace std;

namespace llvm {

  class Transformation {
    public:
      Transformation() {
        for (int i = 0; i < FuncUnit::NumFuncUnits; i++)
          usageChange[i] = 0;
      }
      virtual void applyTransformation() = 0;
      virtual bool canTransform(Instruction *I) = 0;
      std::array<int, FuncUnit::NumFuncUnits> usageChange;
  };

  class ShlToMul : public Transformation {
    public:
      ShlToMul() : Transformation() {
        usageChange[FuncUnit::Shift] = -1;
        // TODO: What if it's FP?
        usageChange[FuncUnit::IntMul] = 1;
    }
    void applyTransformation() override {
      errs() << "ShlToMul\n";
    };
    bool canTransform(Instruction *I) override { return true;}
  };

  class ShrToDiv : public Transformation {
    public:
      ShrToDiv() : Transformation() {
        usageChange[FuncUnit::Shift] = -1;
        // TODO: What if it's FP?
        // TODO: Which FU is used by division?
        usageChange[FuncUnit::IntMul] = 1;
    }
    void applyTransformation() override {
      errs() << "ShlToDiv\n";
    };
    bool canTransform(Instruction *I) override { return true;}
  };

  class BalanceFunctionalUnits : public FunctionPass {
    public:
      static char ID;

      BalanceFunctionalUnits() : FunctionPass(ID) {}

      void getAnalysisUsage(AnalysisUsage &AU) const override;
      bool runOnFunction(Function &F) override;
    private:
      float overuseRateThreshold = FLT_MAX;
      BlockFrequencyInfo *BFI;

      pair<Transformation*, Instruction*> selectNextTransformation(Function &F, array<unsigned long, FuncUnit::NumFuncUnits> usage);
      array<unsigned long, FuncUnit::NumFuncUnits> transformationEffect(Transformation *tsfm, Instruction *I, array<unsigned long, FuncUnit::NumFuncUnits> usage);
      float overuseRate(array<unsigned long, FuncUnit::NumFuncUnits> usage);
      vector<Transformation*> transformations = {new ShlToMul, new ShrToDiv};
  };

} // end namespace
#endif
