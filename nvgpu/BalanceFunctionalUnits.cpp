#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/IR/IRBuilder.h"

#include "InstructionMixAnalysis.h"
#include "Transformations.h"
#include "BalanceFunctionalUnits.h"

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "fu-balance"

bool BalanceFunctionalUnits::runOnLoop(Loop *L, LPPassManager &LPM) {
  if(L->getSubLoops().size() > 0)
    return false; // Abort, not an innermost loop

  array<unsigned long, FuncUnit::NumFuncUnits> usage = getAnalysis<InstructionMixAnalysis>().getUsage();
  BFI = &getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();
  bool changed = false;

  while (true) {
    pair<Transformation*, Instruction*> next = selectNextTransformation(L, usage);
    Transformation *tsfm = get<0>(next);
    Instruction *inst = get<1>(next);
    if (tsfm == nullptr) {
      break;
    }
    tsfm->applyTransformation(inst);
    usage = transformationEffect(tsfm, inst, usage);
    changed = true;
  }

  return changed;;
}

void BalanceFunctionalUnits::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<InstructionMixAnalysis>();
  AU.addRequired<BlockFrequencyInfoWrapperPass>();
  AU.setPreservesCFG();
}

pair<Transformation*, Instruction*> BalanceFunctionalUnits::selectNextTransformation(Loop *L, array<unsigned long, FuncUnit::NumFuncUnits> usage) {
  vector<pair<Transformation*, Instruction*> > candidates;
  Transformation *bestTransformation = nullptr;
  Instruction *bestInstruction = nullptr;
  float minOveruseRate = overuseRateThreshold;

  for (vector<Transformation*>::iterator tsfm = transformations.begin(); tsfm != transformations.end(); tsfm++) {
    for(Loop::block_iterator block = L->block_begin(), blockEnd = L->block_end(); block!= blockEnd; ++block) {
      BasicBlock *B = *block;
      for(BasicBlock::iterator I = B->begin(), E = B->end(); I !=E; I++) {
        if ((*tsfm)->canTransform(&*I)) {
          array<unsigned long, FuncUnit::NumFuncUnits> transformedUsage = transformationEffect(*tsfm, &*I, usage);
          float overuse = overuseRate(transformedUsage);
          if (overuse < minOveruseRate) {
            minOveruseRate = overuse;
            bestTransformation = *tsfm;
            bestInstruction = &*I;
          }
          candidates.push_back(make_pair(*tsfm, &*I));
        }
      }
    }
  }

  return make_pair(bestTransformation, bestInstruction);
}

array<unsigned long, FuncUnit::NumFuncUnits> BalanceFunctionalUnits::transformationEffect(Transformation *tsfm, Instruction *I, array<unsigned long, FuncUnit::NumFuncUnits> usage) {
  array<unsigned long, FuncUnit::NumFuncUnits> transformedUsage;
  for (int fu = 0; fu < FuncUnit::NumFuncUnits; fu++) {
    transformedUsage[fu] = usage[fu] + tsfm->usageChange[fu] * BFI->getBlockFreq(I->getParent()).getFrequency();
  }
  return transformedUsage;
}

float BalanceFunctionalUnits::overuseRate(array<unsigned long, FuncUnit::NumFuncUnits> usage) {
  float usageTotal = 0;
  for (int fu = 0; fu < FuncUnit::NumFuncUnits; fu++) {
    usageTotal += usage[fu];
  }

  float overuse = 0;
  // The -1 is because Pseudo instructions have no functional units
  for (int fu = 0; fu < FuncUnit::NumFuncUnits - 1; fu++) {
    if (usage[fu]/usageTotal > sm_35[fu]/256) {
      overuse += (usage[fu]/usageTotal) / (sm_35[fu]/256.0);
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
