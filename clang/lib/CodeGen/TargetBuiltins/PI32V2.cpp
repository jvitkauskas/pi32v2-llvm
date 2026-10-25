//===------ PI32V2.cpp - Emit LLVM Code for PI32V2 builtins ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// JieLi's __builtin_pi32v2_* (clang/Basic/BuiltinsPI32V2.def): the bit
// operations as generic IR, which the back end selects into clz, cls, rev8,
// uextra and sextra again; the rest as the instruction in inline assembly.
//
//===----------------------------------------------------------------------===//

#include "CGBuiltin.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"

using namespace clang;
using namespace CodeGen;
using namespace llvm;

// The instruction itself: asm volatile(Text ::: "memory"), with an address
// operand or a result when Constraints asks for one.
static Value *emitAsm(CodeGenFunction &CGF, StringRef Text,
                      StringRef Constraints, llvm::Type *Ret, ArrayRef<Value *> Args,
                      bool SideEffects) {
  SmallVector<llvm::Type *, 2> Types;
  for (Value *A : Args)
    Types.push_back(A->getType());
  llvm::FunctionType *FTy = llvm::FunctionType::get(Ret, Types, false);
  InlineAsm *IA = InlineAsm::get(FTy, Text, Constraints, SideEffects);
  return CGF.Builder.CreateCall(IA, Args);
}

Value *CodeGenFunction::EmitPI32V2BuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E) {
  llvm::Type *I32 = Builder.getInt32Ty();
  llvm::Type *Void = Builder.getVoidTy();
  auto Arg = [&](unsigned N) { return EmitScalarExpr(E->getArg(N)); };
  auto ConstArg = [&](unsigned N) {
    return E->getArg(N)->EvaluateKnownConstInt(getContext()).getZExtValue();
  };
  switch (BuiltinID) {
  default:
    return nullptr;
  case PI32V2::BI__builtin_pi32v2_cli:
    return emitAsm(*this, "cli", "~{memory}", Void, {}, true);
  case PI32V2::BI__builtin_pi32v2_sti:
    return emitAsm(*this, "sti", "~{memory}", Void, {}, true);
  case PI32V2::BI__builtin_pi32v2_csync:
    return emitAsm(*this, "csync", "~{memory}", Void, {}, true);
  case PI32V2::BI__builtin_pi32v2_nop:
    return emitAsm(*this, "nop", "", Void, {}, true);
  case PI32V2::BI__builtin_pi32v2_trigger:
    return emitAsm(*this, "trigger", "~{memory}", Void, {}, true);
  case PI32V2::BI__builtin_pi32v2_lockset:
    return emitAsm(*this, "lockset", "~{memory}", Void, {}, true);
  case PI32V2::BI__builtin_pi32v2_lockclr:
    return emitAsm(*this, "lockclr", "~{memory}", Void, {}, true);
  case PI32V2::BI__builtin_pi32v2_flush:
    return emitAsm(*this, "flush [$0]", "r,~{memory}", Void, {Arg(0)}, true);
  case PI32V2::BI__builtin_pi32v2_flushinv:
    return emitAsm(*this, "flushinv [$0]", "r,~{memory}", Void, {Arg(0)},
                   true);
  case PI32V2::BI__builtin_pi32v2_iflush:
    return emitAsm(*this, "iflush [$0]", "r,~{memory}", Void, {Arg(0)}, true);
  case PI32V2::BI__builtin_pi32v2_pfetch:
    return emitAsm(*this, "pfetch [$0]", "r", Void, {Arg(0)}, true);
  case PI32V2::BI__builtin_pi32v2_zcmp:
    return emitAsm(*this, "$0 = zcmp($1)", "=r,r", I32, {Arg(0)}, false);
  case PI32V2::BI__builtin_pi32v2_clz: // clz(0) = 32
    return Builder.CreateBinaryIntrinsic(Intrinsic::ctlz, Arg(0),
                                         Builder.getFalse());
  case PI32V2::BI__builtin_pi32v2_cls: { // the leading bits equal to bit 31
    Value *X = Arg(0);
    Value *S = Builder.CreateXor(X, Builder.CreateAShr(X, 31));
    return Builder.CreateBinaryIntrinsic(Intrinsic::ctlz, S,
                                         Builder.getFalse());
  }
  case PI32V2::BI__builtin_pi32v2_popcount:
    return Builder.CreateUnaryIntrinsic(Intrinsic::ctpop, Arg(0));
  case PI32V2::BI__builtin_pi32v2_rev8:
    return Builder.CreateUnaryIntrinsic(Intrinsic::bswap, Arg(0));
  case PI32V2::BI__builtin_pi32v2_uextra:
  case PI32V2::BI__builtin_pi32v2_sextra: {
    // the l bits from bit p (bits past 31 read as 0); sextra sign-extends a
    // field that ends inside the word
    Value *X = Arg(0);
    uint64_t P = ConstArg(1), L = ConstArg(2);
    if (L == 0 || P >= 32)
      return ConstantInt::get(I32, 0);
    bool Signed = BuiltinID == PI32V2::BI__builtin_pi32v2_sextra;
    if (Signed && P + L <= 32)
      return Builder.CreateAShr(Builder.CreateShl(X, 32 - P - L), 32 - L);
    Value *V = Builder.CreateLShr(X, P);
    if (P + L < 32)
      V = Builder.CreateAnd(V, (uint64_t(1) << L) - 1);
    return V;
  }
  }
}
