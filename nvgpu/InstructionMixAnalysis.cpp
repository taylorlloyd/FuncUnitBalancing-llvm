#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/Instructions.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "instmix"

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
      unsigned long usage[FuncUnit::NumFuncUnits];
    private:
      BlockFrequencyInfo *BFI;

      FuncUnit unitForInst(Instruction *i);
  };

} // end namespace

bool InstructionMixAnalysis::runOnFunction(Function &F) {
  BFI = &getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();

  for(int i = 0; i < FuncUnit::NumFuncUnits; i++) {
    usage[i] = 0;
  }
  for(Function::iterator BB = F.begin(), E = F.end(); BB != E; BB++) {
    BasicBlock *b = &*BB;
    int freq = BFI->getBlockFreq(b).getFrequency();
    for(BasicBlock::iterator i = b->begin(), e = b->end(); i !=e; i++) {
      FuncUnit fu = unitForInst(&*i);
      usage[fu] += freq;
    }
  }

  DEBUG(errs() << F.getName() << "\n");
  for (int i = 0; i < FuncUnit::NumFuncUnits; i++) {
    DEBUG(errs() << FuncUnitNames[i] << " " << usage[i] << "\n");
  }
  DEBUG(errs() << "\n");

  return false;
}

void InstructionMixAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  // This analysis does not make any modifications
  AU.addRequired<BlockFrequencyInfoWrapperPass>();
  AU.setPreservesAll();
}

FuncUnit InstructionMixAnalysis::unitForInst(Instruction *i) {
  Type *tpe = i->getType();

  // Handle conversion operations
  if(auto C=dyn_cast<CastInst>(i)) {
    Type *to = C->getType();
    Type *from = C->getOperand(0)->getType();
    if(to->isDoubleTy() ||
       from->isDoubleTy() ||
       (to->isIntegerTy() && to->getIntegerBitWidth() == 64) ||
       (from->isIntegerTy() && from->getIntegerBitWidth() == 64))
      return FuncUnit::Conv64;
    if(to->isFloatTy() || (to->isIntegerTy() && to->getIntegerBitWidth() == 32))
      return FuncUnit::Conv32;
    return FuncUnit::Conv;
  }

  // Handle arithmetics
  if(auto BO=dyn_cast<BinaryOperator>(i)) {
    if(tpe->isFloatTy())
      return FuncUnit::FP32;
    if(tpe->isDoubleTy())
      return FuncUnit::FP64;
    if(tpe->isIntegerTy()) {
      // TODO: Look at users to identify MAD promotions
      // TODO: Figure out what happens for a divide
      switch(BO->getOpcode()) {
        case BinaryOperator::Add:
        case BinaryOperator::Sub:
          return FuncUnit::IntAdd;
        case BinaryOperator::Mul:
          return FuncUnit::IntMul;
        case BinaryOperator::And:
        case BinaryOperator::Or:
        case BinaryOperator::Xor:
          return FuncUnit::Logic;
        case BinaryOperator::Shl:
        case BinaryOperator::AShr:
        case BinaryOperator::LShr:
          return FuncUnit::Shift;
        default:
          DEBUG(errs() << "Unrecognized BinaryOp "; BO->dump());
          return FuncUnit::Pseudo;
      }
    }
  }

  // Handle Comparisons
  if(auto CMP=dyn_cast<CmpInst>(i)) {
    return FuncUnit::Logic;
  }

  // Handle memory operations
  if(dyn_cast<LoadInst>(i) || dyn_cast<StoreInst>(i)) {
    return FuncUnit::Mem;
  }

  // Handle control flow operations (branch, return, etc.)
  if(dyn_cast<TerminatorInst>(i)) {
    return FuncUnit::Control;
  }

  //TODO: Figure out what to do with getelementptr, call and alloca
  if(dyn_cast<GetElementPtrInst>(i)) {
    return FuncUnit::Pseudo;
  }
  if(dyn_cast<CallInst>(i)) {
    return FuncUnit::Pseudo;
  }
  if(dyn_cast<AllocaInst>(i)) {
    return FuncUnit::Pseudo;
  }
  if(isa<PHINode>(i)) {
    return FuncUnit::Pseudo;
  }

  //TODO: handle NVidia libdevice calls

  DEBUG(errs() << "Unrecognized Instruction "; i->dump());
  return FuncUnit::Pseudo;
}

char InstructionMixAnalysis::ID = 0;
static RegisterPass<InstructionMixAnalysis> X("gpumix", "Reports estimated instruction mixes for GPU functions",
                                        false,
                                        true);

static void registerMyPass(const PassManagerBuilder &, legacy::PassManagerBase &PM) {
  PM.add(new InstructionMixAnalysis());
}
static RegisterStandardPasses RegisterMyPass(PassManagerBuilder::EP_OptimizerLast, registerMyPass);
