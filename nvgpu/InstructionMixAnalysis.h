#ifndef INSTRUCTION_MIX_ANALYSIS_H
#define INSTRUCTION_MIX_ANALYSIS_H
#include "llvm/Analysis/BlockFrequencyInfo.h"
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

  class InstructionMixAnalysis : public FunctionPass {
    public:
      static char ID;

      InstructionMixAnalysis() : FunctionPass(ID) {}

      void getAnalysisUsage(AnalysisUsage &AU) const override;
      bool runOnFunction(Function &F) override;
      std::array<unsigned long, FuncUnit::NumFuncUnits> const &getUsage() const { return usage; }
    private:
      BlockFrequencyInfo *BFI;
      std::array<unsigned long, FuncUnit::NumFuncUnits> usage;

      FuncUnit unitForInst(Instruction *i);
  };

} // end namespace
#endif
