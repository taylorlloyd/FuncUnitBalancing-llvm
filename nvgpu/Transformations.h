#ifndef TRANSFORMATIONS_H
#define TRANSFORMATIONS_H

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

  class ShlToMul : public Transformation{
    public:
      ShlToMul();
      void applyTransformation(Instruction *I) override;
      bool canTransform(Instruction *I) override;
  };

  class ShrToDiv : public Transformation{
    public:
      ShrToDiv();
      void applyTransformation(Instruction *I) override;
      bool canTransform(Instruction *I) override;
  };

  class MulToShl : public Transformation{
    public:
      MulToShl();
      void applyTransformation(Instruction *I) override;
      bool canTransform(Instruction *I) override;
  };


  class Cvt32ToCvt64 : public Transformation{
    public:
      Cvt32ToCvt64();
      void applyTransformation(Instruction *I) override;
      bool canTransform(Instruction *I) override;
  };

}
#endif
