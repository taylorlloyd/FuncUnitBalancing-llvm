#ifndef BALANCE_FUNCTIONAL_UNITS_H
#define BALANCE_FUNCTIONAL_UNITS_H

using namespace std;

namespace llvm {

  class Transformation {
    public:
      virtual void applyTransformation() = 0;
      virtual bool canTransform(Instruction *I) = 0;
  };

  class ShlToMul : public Transformation {
    void applyTransformation() override {
      errs() << "ShlToMul\n";
    };
    bool canTransform(Instruction *I) override { return true;}
  };

  class ShrToDiv : public Transformation {
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
      BlockFrequencyInfo *BFI;

      Transformation *selectNextTransformation(Function &F);
      array<unsigned long, FuncUnit::NumFuncUnits> transformationEffect(Transformation *tsfm, array<unsigned long, FuncUnit::NumFuncUnits> usage);
      float overuseRate(array<unsigned long, FuncUnit::NumFuncUnits> usage);
      vector<Transformation*> transformations = {new ShlToMul, new ShrToDiv};
  };

} // end namespace
#endif
