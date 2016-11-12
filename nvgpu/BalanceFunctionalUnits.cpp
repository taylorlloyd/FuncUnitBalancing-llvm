#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "InstructionMixAnalysis.h"
#include "BalanceFunctionalUnits.h"

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "fu-balance"

bool BalanceFunctionalUnits::runOnFunction(Function &F) {
  array<unsigned long, FuncUnit::NumFuncUnits> usage = getAnalysis<InstructionMixAnalysis>().getUsage();
  BFI = &getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();
  bool changed = false;

  while (Transformation *tsfm = selectNextTransformation(F)) {
    tsfm->applyTransformation();
    usage = transformationEffect(tsfm, usage);
    changed = true;
  }

  return changed;;
}

void BalanceFunctionalUnits::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<InstructionMixAnalysis>();
  AU.addRequired<BlockFrequencyInfoWrapperPass>();
  AU.setPreservesCFG();
}

Transformation* BalanceFunctionalUnits::selectNextTransformation(Function &F) {
  vector<pair<Transformation*, Instruction*> > candidates;
  for (vector<Transformation*>::iterator tsfm = transformations.begin(); tsfm != transformations.end(); tsfm++) {
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      if ((*tsfm)->canTransform(&*I)) {
        candidates.push_back(make_pair(*tsfm, &*I));
      }
    }
    (*tsfm)->applyTransformation();
  }
  //TODO: get pair with minimum overuserate

  return nullptr;
}

array<unsigned long, FuncUnit::NumFuncUnits> BalanceFunctionalUnits::transformationEffect(Transformation *tsfm, array<unsigned long, FuncUnit::NumFuncUnits> usage) {
  array<unsigned long, FuncUnit::NumFuncUnits> transformationedUsage;
  for (int fu = 0; fu < FuncUnit::NumFuncUnits; fu++) {
    //TODO: complete expression
    transformationedUsage[fu] = usage[fu];
  }
  return transformationedUsage;
}

float BalanceFunctionalUnits::overuseRate(array<unsigned long, FuncUnit::NumFuncUnits> usage) {
  float usageTotal = 0;
  for (int fu = 0; fu < FuncUnit::NumFuncUnits; fu++) {
    usageTotal += usage[fu];
  }

  float overuse = 0;
  for (int fu = 0; fu < FuncUnit::NumFuncUnits; fu++) {
    if (usage[fu]/usageTotal > sm_35[fu]/256) {
      overuse += (usage[fu]/usageTotal) / (sm_35[fu]/256);
    }
  }

  return overuse;
}

char BalanceFunctionalUnits::ID = 0;
static RegisterPass<BalanceFunctionalUnits> X("fu-balance", "Balance GPU Functional Units",
                                        false,
                                        false);

static void registerMyPass(const PassManagerBuilder &, legacy::PassManagerBase &PM) {
  PM.add(new BalanceFunctionalUnits());
}
static RegisterStandardPasses RegisterMyPass(PassManagerBuilder::EP_OptimizerLast, registerMyPass);
