#include "llvm/IR/Instructions.h"

#include "llvm/IR/IRBuilder.h"

#include "InstructionMixAnalysis.h"
#include "Transformations.h"

#include <cmath>

using namespace llvm;

/**** ShlToMul ****/
ShlToMul::ShlToMul() : Transformation() {
  usageChange[FuncUnit::Shift] = -1;
  usageChange[FuncUnit::IntMul] = 1;
}

void ShlToMul::applyTransformation(Instruction *I) {

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

bool ShlToMul::canTransform(Instruction *I) {
  if (dyn_cast<ShlOperator>(I) && dyn_cast<ConstantInt>(I->getOperand(1))) {
    return true;
  }
  return false;
}

/**** ShrToDiv ****/
ShrToDiv::ShrToDiv() : Transformation() {
  usageChange[FuncUnit::Shift] = -1;
  // TODO: What if it's FP?
  // TODO: Which FU is used by division?
  usageChange[FuncUnit::IntMul] = 1;
}

void ShrToDiv::applyTransformation(Instruction *I) {
  errs() << "ShrToDiv\n";
};

bool ShrToDiv::canTransform(Instruction *I) {
  if (dyn_cast<AShrOperator>(I) || dyn_cast<LShrOperator>(I))
    return true;
  return false;
}

/**** MulToShl ****/
MulToShl::MulToShl() : Transformation() {
  usageChange[FuncUnit::Shift] = 1;
  usageChange[FuncUnit::IntMul] = -1;
}

void MulToShl::applyTransformation(Instruction *I) {

    IRBuilder<> builder(I);
    Value *op1 = I->getOperand(0);
    ConstantInt *op2 = dyn_cast<ConstantInt>(I->getOperand(1));
    int64_t op2Value = op2->getSExtValue();
    bool hasNUW = I->hasNoUnsignedWrap();
    bool hasNSW = I->hasNoSignedWrap();

    Value *op2New = ConstantInt::get(op2->getType(), log2(op2Value));

    Value *shl = builder.CreateShl(op1, op2New, "", hasNUW, hasNSW);

    for (auto &U : I->uses()) {
      User *user = U.getUser();
      user->setOperand(U.getOperandNo(), shl);
    }

    I->eraseFromParent();
  };

bool MulToShl::canTransform(Instruction *I) {
  if (I->getOpcode() == BinaryOperator::Mul) {
    /* Check if operand is constant and power of two */
    if (ConstantInt *op = dyn_cast<ConstantInt>(I->getOperand(1))) {
      int64_t opValue = op->getSExtValue();
      if (!(opValue & (opValue -1))) {
        return true;
      }
    }
  }
  return false;
}

/**** Cvt32ToCvt64 ****/
Cvt32ToCvt64::Cvt32ToCvt64() : Transformation() {
  usageChange[FuncUnit::Conv32] = -2;
  usageChange[FuncUnit::FP32] = -1;
  usageChange[FuncUnit::Conv64] = 3; /* convert twice to 64 and once from 64 */
  usageChange[FuncUnit::FP64] = 1;
}

void Cvt32ToCvt64::applyTransformation(Instruction *I) {

  // Preconditions
  assert(I->getType()->isFloatTy());
  assert(isa<BinaryOperator>(I));

  LLVMContext& ctx = I->getContext();
  BinaryOperator *BO = dyn_cast<BinaryOperator>(I);
  vector<Instruction *> rm;

  // Collect our intermediate and target types
  Type *fTy = I->getType();
  Type *dTy = Type::getDoubleTy(ctx);

  // Collect our operands
  Value *op1 = I->getOperand(0);
  Value *op2 = I->getOperand(1);

  Instruction::CastOps co1 = Instruction::CastOps::FPExt;
  Instruction::CastOps co2 = Instruction::CastOps::FPExt;

  // Drop unnecessary casts
  if(auto c1 = dyn_cast<CastInst>(op1)) {
    if(auto op = CastInst::isEliminableCastPair(
                                      c1->getOpcode(),
                                      CastInst::FPExt,
                                      c1->getSrcTy(),
                                      c1->getDestTy(),
                                      dTy,
                                      nullptr,
                                      nullptr,
                                      nullptr)) {
      op1=c1->getOperand(0);
      co1=(Instruction::CastOps) op;
      if(c1->getNumUses() > 1)
        rm.push_back(c1);
    }
  }
  if(auto c2 = dyn_cast<CastInst>(op2)) {
    if(auto op = CastInst::isEliminableCastPair(
                                      c2->getOpcode(),
                                      CastInst::FPExt,
                                      c2->getSrcTy(),
                                      c2->getDestTy(),
                                      dTy,
                                      nullptr,
                                      nullptr,
                                      nullptr)) {
      op2=c2->getOperand(0);
      co2=(Instruction::CastOps) op;
      if(c2->getNumUses() > 1)
        rm.push_back(c2);
    }
  }

  Instruction* newOp1 = CastInst::Create(co1, op1, dTy, "up_op1", I);
  Instruction* newOp2 = CastInst::Create(co2, op2, dTy, "up_op2", I);

  Instruction *newI = BinaryOperator::Create(BO->getOpcode(), newOp1, newOp2, "up_fp", I);

  Instruction *repl = CastInst::CreateFPCast(newI, fTy, "down_fp", I);

  I->replaceAllUsesWith(repl);
  rm.push_back(I);

  while(!rm.empty()) {
    Instruction *i = rm.back();
    i->eraseFromParent();
    rm.pop_back();
  }
};

bool Cvt32ToCvt64::canTransform(Instruction *I) {
  /* Is it an operation? */
  if (!isa<BinaryOperator>(I)){
    return false;
  }

  /* Are the operands cast instructions? */
  CastInst *op1 = dyn_cast<CastInst>(I->getOperand(0));
  CastInst *op2 = dyn_cast<CastInst>(I->getOperand(1));
  if (!op1 || !op2) {
    return false;
  }

  /* Are they conversion to float? */
  Type *to1 = op1->getType();
  Type *to2 = op2->getType();
  if (!to1->isFloatTy() || !to2->isFloatTy()) {
    return false;
  }

  /* Are the casts used only once? */
  if (op1->getNumUses() > 1 || op2->getNumUses() > 1)
    return false;

  return true;
}
