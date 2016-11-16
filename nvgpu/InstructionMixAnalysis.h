#ifndef INSTRUCTION_MIX_ANALYSIS_H
#define INSTRUCTION_MIX_ANALYSIS_H

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/LoopPass.h"

#include <array>
#include <vector>

using namespace std;

namespace llvm {
  enum FuncUnit {
    FP32,
    FP64,
    Trans,
    IntAdd,
    IntMul,
    Shift,
    Bitfield,
    Logic,
    Warp,
    Conv32,
    Conv64,
    Conv,
    Mem,
    Control,
    Pseudo, // Used to indicate no cycle will be used (debug, etc)
    NumFuncUnits,
  };

  const char* FuncUnitNames[] {
    "FP32",
    "FP64",
    "Trans",
    "IntAdd",
    "IntMul",
    "Shift",
    "Bitfield",
    "Logic",
    "Warp",
    "Conv32",
    "Conv64",
    "Conv",
    "Mem",
    "Control",
    "Pseudo", // Used to indicate no cycle will be used (debug, etc)
  };

  const int sm_35[] = {
   192, // FP32,
   64,  // FP64,
   32,  // Trans,
   160, // IntAdd,
   160, // IntMul,
   64,  // Shift,
   64,  // Bitfield,
   160, // Logic,
   32,  // Warp,
   128, // Conv32,
   32,  // Conv64,
   32,  // Conv,
   256, // Mem,
   256  // Control
  };

  class InstructionMixAnalysis : public LoopPass {
    public:
      static char ID;

      InstructionMixAnalysis() : LoopPass(ID) {}

      void getAnalysisUsage(AnalysisUsage &AU) const override;
      bool runOnLoop(Loop *l, LPPassManager &LPM) override;
      array<unsigned long, FuncUnit::NumFuncUnits> const &getUsage() const { return usage; }
    private:
      array<unsigned long, FuncUnit::NumFuncUnits> usage;

      vector<FuncUnit> unitForInst(Instruction *i);
      bool canFuseMultAdd(BinaryOperator *add);
      bool canBitfieldExtract(BinaryOperator *andi);
      void pushInstructionsForCall(CallInst* CI, vector<FuncUnit> units);
      void pushInstructionsForGEP(GetElementPtrInst* GEP, vector<FuncUnit> units);
  };

} // end namespace
#endif
