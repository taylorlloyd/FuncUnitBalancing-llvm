#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "InstructionMixAnalysis.h"

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "instmix"

namespace llvm {
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
}

bool InstructionMixAnalysis::runOnLoop(Loop *L, LPPassManager &LPM) {
  if(L->getSubLoops().size() > 0)
    return false; // Abort, not an innermost loop

  // Zero-initialize usage
  for(int i = 0; i < FuncUnit::NumFuncUnits; i++) {
    usage[i] = 0;
  }

  for(Loop::block_iterator block = L->block_begin(), blockEnd = L->block_end(); block!= blockEnd; ++block) {
    BasicBlock *B = *block;
    for(BasicBlock::iterator I = B->begin(), E = B->end(); I !=E; I++) {
      vector<FuncUnit> freq = unitForInst(&*I);
	  for (FuncUnit fu : freq) {
		usage[fu]++;
	  }
	}
  }

  for (int i = 0; i < FuncUnit::NumFuncUnits; i++) {
    DEBUG(errs() << FuncUnitNames[i] << " " << usage[i] << "\n");
  }
  DEBUG(errs() << "\n");

  return false;
}

void InstructionMixAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

vector<FuncUnit> InstructionMixAnalysis::unitForInst(Instruction *i) {
  Type *tpe = i->getType();
  vector<FuncUnit> ret;

  // Handle conversion operations
  if(auto C=dyn_cast<CastInst>(i)) {
    Type *to = C->getType();
    Type *from = C->getOperand(0)->getType();
    if(to->isDoubleTy() ||
       from->isDoubleTy() ||
       (to->isIntegerTy() && to->getIntegerBitWidth() == 64) ||
       (from->isIntegerTy() && from->getIntegerBitWidth() == 64)) {
      ret.push_back(FuncUnit::Conv64);

    } else if(to->isFloatTy() || (to->isIntegerTy() && to->getIntegerBitWidth() == 32)) {
      ret.push_back(FuncUnit::Conv32);

    } else {
      ret.push_back(FuncUnit::Conv);
    }
    return ret;
  }

  // Handle arithmetics
  if(auto BO=dyn_cast<BinaryOperator>(i)) {
    if(tpe->isFloatTy())
      ret.push_back(FuncUnit::FP32);
    else if(tpe->isDoubleTy())
      ret.push_back(FuncUnit::FP64);
    else if(tpe->isIntegerTy()) {
      // TODO: Figure out what happens for a divide
      switch(BO->getOpcode()) {
        case BinaryOperator::Add:
        case BinaryOperator::Sub:
          if(!canFuseMultAdd(BO))
            ret.push_back(FuncUnit::IntAdd);
          break;
        case BinaryOperator::Mul:
          ret.push_back(FuncUnit::IntMul);
          break;
        case BinaryOperator::And:
          if(canBitfieldExtract(BO))
            ret.push_back(FuncUnit::Bitfield);
          else
            ret.push_back(FuncUnit::Logic);
          break;
        case BinaryOperator::Or:
        case BinaryOperator::Xor:
          ret.push_back(FuncUnit::Logic);
          break;
        case BinaryOperator::Shl:
        case BinaryOperator::AShr:
        case BinaryOperator::LShr:
          ret.push_back(FuncUnit::Shift);
          break;
        default:
          DEBUG(errs() << "Unrecognized BinaryOp "; BO->dump());
          ret.push_back(FuncUnit::Pseudo);
      }
    }
    return ret;
  }

  // Handle Comparisons
  if(auto CMP=dyn_cast<CmpInst>(i)) {
    ret.push_back(FuncUnit::Logic);
    return ret;
  }

  // Handle memory operations
  if(dyn_cast<LoadInst>(i) || dyn_cast<StoreInst>(i)) {
    ret.push_back(FuncUnit::Mem);
    return ret;
  }

  // Handle control flow operations (branch, return, etc.)
  if(dyn_cast<TerminatorInst>(i)) {
    ret.push_back(FuncUnit::Control);
    return ret;
  }

  if(auto GEP=dyn_cast<GetElementPtrInst>(i)) {
    pushInstructionsForGEP(GEP, ret);
    return ret;
  }
  if(auto CI=dyn_cast<CallInst>(i)) {
    pushInstructionsForCall(CI, ret);
    return ret;
  }
  if(dyn_cast<AllocaInst>(i)) {
    return ret;
  }
  if(isa<PHINode>(i)) {
    return ret;
  }

  //TODO: handle NVidia libdevice calls

  DEBUG(errs() << "Unrecognized Instruction "; i->dump());
  return ret;
}

bool InstructionMixAnalysis::canFuseMultAdd(BinaryOperator *add) {
  for(auto o=add->op_begin(),e=add->op_end(); o!=e; ++o) {
    if(auto mult=dyn_cast<BinaryOperator>(*o)) {
      // To fuse, the add/sub must be the only use
      if(mult->getNumUses() != 1)
        continue;

      // It also needs to be a multiply
      if(mult->getOpcode() != BinaryOperator::Mul &&
         mult->getOpcode() != BinaryOperator::FMul)
        continue;

      // Well then, these can probably be fused!
      return true;
    }
  }
  return false;
}

bool InstructionMixAnalysis::canBitfieldExtract(BinaryOperator *andi) {
  if(!dyn_cast<ConstantExpr>(andi->getOperand(1)))
    return false; // Must shift by a constant

  auto shr = dyn_cast<BinaryOperator>(andi->getOperand(0));
  if(!shr || (shr->getOpcode() != BinaryOperator::AShr &&
              shr->getOpcode() != BinaryOperator::LShr))
    return false; // Need to be anding off a right-shift

  if(shr->getNumUses() != 1)
    return false; // Need the intermediate product

  return true;
}

void InstructionMixAnalysis::pushInstructionsForCall(CallInst* CI, vector<FuncUnit> units) {
  Function *F = CI->getFunction();

  if(!F)
    // Function pointer call. Abandon all hope
    return;

  if(F->getIntrinsicID() != Intrinsic::not_intrinsic) {
    // This is an intrinsic function
    switch(F->getIntrinsicID()) {
      case Intrinsic::nvvm_max_i:
      case Intrinsic::nvvm_max_ui:
      case Intrinsic::nvvm_min_i:
      case Intrinsic::nvvm_min_ui:
      case Intrinsic::nvvm_max_ll:
      case Intrinsic::nvvm_max_ull:
        units.push_back(FuncUnit::IntAdd);
        break;
      case Intrinsic::nvvm_move_i16:
      case Intrinsic::nvvm_move_i32:
      case Intrinsic::nvvm_move_i64:
      case Intrinsic::nvvm_move_ptr:
      case Intrinsic::nvvm_move_float:
      case Intrinsic::nvvm_move_double:
        units.push_back(FuncUnit::Pseudo);
        break;
      case Intrinsic::nvvm_fmax_f:
      case Intrinsic::nvvm_fma_rm_f:
      case Intrinsic::nvvm_fma_rn_f:
      case Intrinsic::nvvm_fma_rp_f:
      case Intrinsic::nvvm_fma_rz_f:
      case Intrinsic::nvvm_fmax_ftz_f:
      case Intrinsic::nvvm_fma_rm_ftz_f:
      case Intrinsic::nvvm_fma_rn_ftz_f:
      case Intrinsic::nvvm_fma_rp_ftz_f:
      case Intrinsic::nvvm_fma_rz_ftz_f:
        units.push_back(FuncUnit::FP32);
        break;
      case Intrinsic::nvvm_fmax_d:
      case Intrinsic::nvvm_fma_rm_d:
      case Intrinsic::nvvm_fma_rn_d:
      case Intrinsic::nvvm_fma_rp_d:
      case Intrinsic::nvvm_fma_rz_d:
        units.push_back(FuncUnit::FP64);
        break;
      case Intrinsic::nvvm_rcp_rm_f:
      case Intrinsic::nvvm_rcp_rn_f:
      case Intrinsic::nvvm_rcp_rp_f:
      case Intrinsic::nvvm_rcp_rz_f:
      case Intrinsic::nvvm_rcp_rm_ftz_f:
      case Intrinsic::nvvm_rcp_rn_ftz_f:
      case Intrinsic::nvvm_rcp_rp_ftz_f:
      case Intrinsic::nvvm_rcp_rz_ftz_f:
      case Intrinsic::nvvm_rcp_approx_ftz_d:

      case Intrinsic::nvvm_rsqrt_approx_f:
      case Intrinsic::nvvm_rsqrt_approx_ftz_f:
      case Intrinsic::nvvm_rsqrt_approx_d:

      case Intrinsic::nvvm_lg2_approx_f:
      case Intrinsic::nvvm_lg2_approx_ftz_f:
      case Intrinsic::nvvm_lg2_approx_d:

      case Intrinsic::nvvm_ex2_approx_f:
      case Intrinsic::nvvm_ex2_approx_ftz_f:
      case Intrinsic::nvvm_ex2_approx_d:

      case Intrinsic::nvvm_sin_approx_f:
      case Intrinsic::nvvm_sin_approx_ftz_f:

      case Intrinsic::nvvm_cos_approx_f:
      case Intrinsic::nvvm_cos_approx_ftz_f:
        units.push_back(FuncUnit::Trans);
        break;
      case Intrinsic::nvvm_sad_i:
      case Intrinsic::nvvm_sad_ui:
      case Intrinsic::nvvm_popc_i:
      case Intrinsic::nvvm_popc_ll:
      case Intrinsic::nvvm_clz_i:
      case Intrinsic::nvvm_clz_ll:
        units.push_back(FuncUnit::IntMul);
        break;
      case Intrinsic::bitreverse:
      // TODO: there seems to be no bitfield insert support at all.
        units.push_back(FuncUnit::Bitfield);
        break;

      case Intrinsic::nvvm_shfl_up_f32:
      case Intrinsic::nvvm_shfl_idx_f32:
      case Intrinsic::nvvm_shfl_down_f32:
      case Intrinsic::nvvm_shfl_bfly_f32:
      case Intrinsic::nvvm_shfl_up_i32:
      case Intrinsic::nvvm_shfl_idx_i32:
      case Intrinsic::nvvm_shfl_down_i32:
      case Intrinsic::nvvm_shfl_bfly_i32:
        units.push_back(FuncUnit::Warp);
        break;

      case Intrinsic::nvvm_bitcast_i2f:
      case Intrinsic::nvvm_bitcast_f2i:
      case Intrinsic::nvvm_f2i_rm:
      case Intrinsic::nvvm_f2i_rn:
      case Intrinsic::nvvm_f2i_rp:
      case Intrinsic::nvvm_f2i_rz:
      case Intrinsic::nvvm_f2i_rm_ftz:
      case Intrinsic::nvvm_f2i_rn_ftz:
      case Intrinsic::nvvm_f2i_rp_ftz:
      case Intrinsic::nvvm_f2i_rz_ftz:
      case Intrinsic::nvvm_f2ui_rm:
      case Intrinsic::nvvm_f2ui_rn:
      case Intrinsic::nvvm_f2ui_rp:
      case Intrinsic::nvvm_f2ui_rz:
      case Intrinsic::nvvm_f2ui_rm_ftz:
      case Intrinsic::nvvm_f2ui_rn_ftz:
      case Intrinsic::nvvm_f2ui_rp_ftz:
      case Intrinsic::nvvm_f2ui_rz_ftz:
      case Intrinsic::nvvm_i2f_rm:
      case Intrinsic::nvvm_i2f_rn:
      case Intrinsic::nvvm_i2f_rp:
      case Intrinsic::nvvm_i2f_rz:
      case Intrinsic::nvvm_ui2f_rm:
      case Intrinsic::nvvm_ui2f_rn:
      case Intrinsic::nvvm_ui2f_rp:
      case Intrinsic::nvvm_ui2f_rz:
      case Intrinsic::nvvm_h2f:
        units.push_back(FuncUnit::Conv32);
        break;

      case Intrinsic::nvvm_f2ll_rm:
      case Intrinsic::nvvm_f2ll_rn:
      case Intrinsic::nvvm_f2ll_rp:
      case Intrinsic::nvvm_f2ll_rz:
      case Intrinsic::nvvm_f2ll_rm_ftz:
      case Intrinsic::nvvm_f2ll_rn_ftz:
      case Intrinsic::nvvm_f2ll_rp_ftz:
      case Intrinsic::nvvm_f2ll_rz_ftz:
      case Intrinsic::nvvm_f2ull_rm:
      case Intrinsic::nvvm_f2ull_rn:
      case Intrinsic::nvvm_f2ull_rp:
      case Intrinsic::nvvm_f2ull_rz:
      case Intrinsic::nvvm_f2ull_rm_ftz:
      case Intrinsic::nvvm_f2ull_rn_ftz:
      case Intrinsic::nvvm_f2ull_rp_ftz:
      case Intrinsic::nvvm_f2ull_rz_ftz:
      case Intrinsic::nvvm_i2d_rm:
      case Intrinsic::nvvm_i2d_rn:
      case Intrinsic::nvvm_i2d_rp:
      case Intrinsic::nvvm_i2d_rz:
      case Intrinsic::nvvm_ui2d_rm:
      case Intrinsic::nvvm_ui2d_rn:
      case Intrinsic::nvvm_ui2d_rp:
      case Intrinsic::nvvm_ui2d_rz:
      case Intrinsic::nvvm_ll2d_rm:
      case Intrinsic::nvvm_ll2d_rn:
      case Intrinsic::nvvm_ll2d_rp:
      case Intrinsic::nvvm_ll2d_rz:
      case Intrinsic::nvvm_ull2d_rm:
      case Intrinsic::nvvm_ull2d_rn:
      case Intrinsic::nvvm_ull2d_rp:
      case Intrinsic::nvvm_ull2d_rz:
        units.push_back(FuncUnit::Conv64);
    }

  } else {
    // This is a user function, just give up now
  }
}

char InstructionMixAnalysis::ID = 0;
static RegisterPass<InstructionMixAnalysis> X("gpumix", "Reports estimated instruction mixes for GPU loops",
                                        false,
                                        true);

static void registerMyPass(const PassManagerBuilder &, legacy::PassManagerBase &PM) {
  PM.add(new InstructionMixAnalysis());
}
static RegisterStandardPasses RegisterMyPass(PassManagerBuilder::EP_OptimizerLast, registerMyPass);
