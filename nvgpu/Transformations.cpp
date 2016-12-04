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
  //errs() << "Starting to apply Cvt\n       " << *I << "\n";
  CastInst *op1 = dyn_cast<CastInst>(I->getOperand(0));
  CastInst *op2 = dyn_cast<CastInst>(I->getOperand(1));

  //errs() << "  op1: " << *op1 << "\n";
  //errs() << "  op2: " << *op2 << "\n";

  LLVMContext& ctx = I->getContext();

  /* Replace cast1 */
  IRBuilder<> builderOp1(op1);
  bool isSigned = (op1->getOpcode() == Instruction::SExt) || (op1->getOpcode() == Instruction::SIToFP);
  Instruction::CastOps opcode = CastInst::getCastOpcode(op1->getOperand(0), isSigned, op1->getDestTy(), true);
  Value *newOp1 = builderOp1.CreateCast(opcode, op1->getOperand(0), Type::getDoubleTy(ctx), "");
  for (auto &U : op1->uses()) {
    User *user = U.getUser();
    user->setOperand(U.getOperandNo(), newOp1);
  }
  op1->eraseFromParent();

  /* Replace cast2 */
  IRBuilder<> builderOp2(op2);
  isSigned = (op2->getOpcode() == Instruction::SExt) || (op2->getOpcode() == Instruction::SIToFP);
  opcode = CastInst::getCastOpcode(op2->getOperand(0), isSigned, op2->getDestTy(), true);
  Value *newOp2 = builderOp2.CreateCast(opcode, op2->getOperand(0), Type::getDoubleTy(ctx), "");
  for (auto &U : op2->uses()) {
    User *user = U.getUser();
    user->setOperand(U.getOperandNo(), newOp2);
  }
  op2->eraseFromParent();

  /* Replace the instruction */
  IRBuilder<> builder(I);
  Instruction *CI = builder.Insert(I->clone());
  I->replaceAllUsesWith(CI);

  /* Convert back to float */
  Value *trunc = builder.CreateFPTrunc(CI, Type::getFloatTy(ctx), "");
  for (auto &U : CI->uses()) {
    User *user = U.getUser();
    if (dyn_cast<Value>(user) != trunc && !dyn_cast<CastInst>(user)) {
      user->setOperand(U.getOperandNo(), trunc);
    }
  }

  I->eraseFromParent();
};

bool Cvt32ToCvt64::canTransform(Instruction *I) {
  /* Is it an operation? */
  if (!dyn_cast<BinaryOperator>(I)){
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

  /* Are they converting from integers (32 bits or less)? */
  Type *from1 = op1->getOperand(0)->getType();
  Type *from2 = op2->getOperand(0)->getType();
  if (from1->isDoubleTy() || (from1->isIntegerTy() && from1->getIntegerBitWidth() > 32) ||
      from2->isDoubleTy() || (from2->isIntegerTy() && from2->getIntegerBitWidth() > 32))
    return false;

  /* Is the result used only once? */
  if (I->getNumUses() > 1)
    return false;

  /* Are the casts used only once? */
  if (op1->getNumUses() > 1 || op2->getNumUses() > 1)
    return false;

  return true;
}
