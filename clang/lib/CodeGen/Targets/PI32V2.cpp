//===- PI32V2.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// JieLi's C convention (pi32v2-isa docs/ABI.md section 5): scalars are words
// of one stream (the backend's calling convention places them); every
// aggregate is passed as a pointer to a copy the caller makes, and returned
// through a hidden pointer, the first slot. va_list is a pointer to the next
// word; va_arg of an aggregate reads a pointer to its copy.
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {
class PI32V2ABIInfo : public DefaultABIInfo {
public:
  PI32V2ABIInfo(CodeGen::CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  ABIArgInfo classifyArgumentType(QualType Ty) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);
    if (isAggregateTypeForABI(Ty)) {
      unsigned AS = getDataLayout().getAllocaAddrSpace();
      if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
        return getNaturalAlignIndirect(Ty, AS,
                                       RAA == CGCXXABI::RAA_DirectInMemory);
      return getNaturalAlignIndirect(Ty, AS, /*ByVal=*/false);
    }
    return DefaultABIInfo::classifyArgumentType(Ty);
  }

  ABIArgInfo classifyReturnType(QualType RetTy) const {
    if (RetTy->isVoidType())
      return ABIArgInfo::getIgnore();
    if (isAggregateTypeForABI(RetTy))
      return getNaturalAlignIndirect(RetTy,
                                     getDataLayout().getAllocaAddrSpace());
    return DefaultABIInfo::classifyReturnType(RetTy);
  }

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    return emitVoidPtrVAArg(CGF, VAListAddr, Ty, isAggregateTypeForABI(Ty),
                            getContext().getTypeInfoInChars(Ty),
                            CharUnits::fromQuantity(4),
                            /*AllowHigherAlign=*/false, Slot);
  }
};

class PI32V2TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  PI32V2TargetCodeGenInfo(CodeGen::CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<PI32V2ABIInfo>(CGT)) {}

  // __attribute__((interrupt)): the backend's "interrupt" function attribute;
  // always_inline_when_const_args: the inliner's.
  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    if (GV->isDeclaration())
      return;
    const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (FD && FD->hasAttr<PI32V2InterruptAttr>())
      cast<llvm::Function>(GV)->addFnAttr("interrupt");
    // __attribute__((always_inline_when_const_args)), as JieLi's clang writes
    // it (the inliner honours it, llvm/lib/Analysis/InlineCost.cpp).
    if (FD && FD->hasAttr<PI32V2AlwaysInlineWhenConstArgsAttr>())
      cast<llvm::Function>(GV)->addFnAttr("always-inline-when-const-args");
  }
};
} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createPI32V2TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<PI32V2TargetCodeGenInfo>(CGM.getTypes());
}
