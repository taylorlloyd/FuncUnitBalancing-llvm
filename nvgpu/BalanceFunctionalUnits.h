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
      virtual void applyTransformation(Instruction *I) = 0;
      virtual bool canTransform(Instruction *I) = 0;
      array<int, FuncUnit::NumFuncUnits> usageChange;
  };

  class ShlToMul : public Transformation {
    public:
      ShlToMul() : Transformation() {
        usageChange[FuncUnit::Shift] = -1;
        usageChange[FuncUnit::IntMul] = 1;
    }
    void applyTransformation(Instruction *I) override {

      BinaryOperator *op = dyn_cast<BinaryOperator>(&*I);
      IRBuilder<> builder(op);
      Value *op1 = op->getOperand(0);
      ConstantInt *op2 = dyn_cast<ConstantInt>(op->getOperand(1));
      int64_t op2Value = op2->getSExtValue();
      bool hasNUW = op->hasNoUnsignedWrap();
      bool hasNSW = op->hasNoSignedWrap();

      Value *op2New = ConstantInt::get(op2->getType(), 1 << op2Value);

      Value *mul = builder.CreateMul(op1, op2New, "", hasNUW, hasNSW);

      for (auto &U : op->uses()) {
        User *user = U.getUser();
        user->setOperand(U.getOperandNo(), mul);
      }

      I->eraseFromParent();
    };

    bool canTransform(Instruction *I) override {
        if (dyn_cast<ShlOperator>(I) && dyn_cast<ConstantInt>(I->getOperand(1))) {
          return true;
        }
        return false;
    }
  };

  class ShrToDiv : public Transformation {
    public:
      ShrToDiv() : Transformation() {
        usageChange[FuncUnit::Shift] = -1;
        // TODO: What if it's FP?
        // TODO: Which FU is used by division?
        usageChange[FuncUnit::IntMul] = 1;
    }
    void applyTransformation(Instruction *I) override {
      errs() << "ShrToDiv\n";
    };
    bool canTransform(Instruction *I) override {
        if (dyn_cast<AShrOperator>(I) || dyn_cast<LShrOperator>(I))
            return true;
        return false;
    }
  };

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
