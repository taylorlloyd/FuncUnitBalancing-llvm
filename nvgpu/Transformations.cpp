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
