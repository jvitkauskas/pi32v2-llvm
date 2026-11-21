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
// word; va_arg of an argument passed by reference (an aggregate, a _BitInt wider than 64 bits)
// reads a pointer to its copy.
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
    QualType Given = Ty;
    Ty = useFirstFieldIfTransparentUnion(Ty);
    unsigned AS = getDataLayout().getAllocaAddrSpace();
    // Passed by reference, a transparent union is a pointer to a copy of the
    // union: its first field's bytes come first, and the copy is as aligned as
    // the union, which the callee takes it to be.
    if (isAggregateTypeForABI(Ty)) {
      if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
        return getNaturalAlignIndirect(Ty, AS,
                                       RAA == CGCXXABI::RAA_DirectInMemory);
      return getNaturalAlignIndirect(Given, AS, /*ByVal=*/false);
    }
    ABIArgInfo Info = DefaultABIInfo::classifyArgumentType(Ty);
    // What DefaultABIInfo passes by reference (a _BitInt wider than 64 bits)
    // goes as an aggregate does: a pointer to a copy the caller makes in its
    // own frame, aligned as the type is. The same pointer word as byval, whose
    // copy the backend makes in the 4-aligned argument area instead.
    if (Info.isIndirect())
      return getNaturalAlignIndirect(Given, AS, /*ByVal=*/false);
    // A transparent union goes as its first field: in that field's slots, not
    // the union's, which an alignment attribute may pad. As the field's
    // in-memory type (i8 for a bool, i64 for a _BitInt(33)), so that no stored
    // bit of another member is lost. The extension is then the in-memory
    // type's: a callee declared with the first field's own type (a _BitInt(7),
    // a bool) gets those bytes as they are, which is that field's value only
    // when they hold one (u.c = 0x55 through a signed char member reaches a
    // _BitInt(7) parameter as 85, not -43).
    if (Ty != Given && Info.canHaveCoerceToType())
      Info.setCoerceToType(CGT.ConvertTypeForMem(Ty));
    return Info;
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
    // Read as the caller passed it (classifyArgumentType). By reference (aggregates, _BitInt wider than
    // 64 bits): a pointer word, to a copy only as aligned as the caller made it. Inline otherwise: a
    // transparent union passed as its first field takes that field's slots, not the union's size.
    // A call site classifies an argument's canonical type: alignment a typedef adds isn't the copy's.
    ABIArgInfo AI = classifyArgumentType(Ty.getCanonicalType());
    TypeInfoChars Info = getContext().getTypeInfoInChars(Ty);
    QualType Passed = useFirstFieldIfTransparentUnion(Ty);
    if (!AI.isIndirect() && Passed != Ty) {
      // A transparent union passed as its first field: the field's stored
      // bytes, from its own slots, become the union's first bytes (the union
      // may be larger). Copied as bytes, as the caller passed them, so a bool
      // or a _BitInt(33) loses no bit of another member.
      TypeInfoChars Field = getContext().getTypeInfoInChars(Passed);
      Address Arg = emitVoidPtrDirectVAArg(
          CGF, VAListAddr, CGF.ConvertTypeForMem(Passed), Field.Width,
          Field.Align, CharUnits::fromQuantity(4), /*AllowHigherAlign=*/false);
      Address Union = CGF.CreateMemTemp(Ty, "va.union");
      CGF.Builder.CreateMemCpy(Union, Arg, Field.Width.getQuantity());
      return CGF.EmitLoadOfAnyValue(CGF.MakeAddrLValue(Union, Ty), Slot);
    }
    if (AI.isIndirect())
      Info.Align = AI.getIndirectAlign();
    return emitVoidPtrVAArg(CGF, VAListAddr, Ty, AI.isIndirect(), Info,
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
