//===--- PI32V2.h - Declare PI32V2 target feature support -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// JieLi's pi32v2 (AC79 series). Type sizes and alignments as JieLi's compiler
// has them (pi32v2-isa docs/ABI.md section 2): ILP32, 64-bit types 4-aligned,
// long double is double, char signed, wchar_t int.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_PI32V2_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_PI32V2_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY PI32V2TargetInfo : public TargetInfo {
  std::string CPU = "generic";

public:
  PI32V2TargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    resetDataLayout();
    LongLongAlign = 32;
    DoubleAlign = 32;
    LongDoubleWidth = 64;
    LongDoubleAlign = 32;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    SuitableAlign = 32;
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
    WCharType = SignedInt;
    MaxAtomicPromoteWidth = 32;
    MaxAtomicInlineWidth = 32; // as JieLi's compiler (read-modify-write ones are __sync_* calls)
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
  bool isValidCPUName(StringRef Name) const override;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;
  bool setCPU(StringRef Name) override;
  bool hasFeature(StringRef Feature) const override;

  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return {};
  }
  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }
  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }
  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    if (*Name == 'r') {
      Info.setAllowsRegister();
      return true;
    }
    return false;
  }
  std::string_view getClobbers() const override { return ""; }
  bool hasBitIntType() const override { return true; }
};
} // namespace targets
} // namespace clang

#endif
