//===--- PI32V2.cpp - Implement PI32V2 target feature support -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

static const char *const GCCRegNames[] = {
    "r0", "r1", "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};

ArrayRef<const char *> PI32V2TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

// JieLi's -mcpu names (r3 is the AC79's), and the FM-1's chip.
static const char *const CPUs[] = {"generic", "r3", "ac791n"};

bool PI32V2TargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::is_contained(CPUs, Name);
}

void PI32V2TargetInfo::fillValidCPUList(SmallVectorImpl<StringRef> &V) const {
  V.append(std::begin(CPUs), std::end(CPUs));
}

bool PI32V2TargetInfo::setCPU(StringRef Name) {
  if (!isValidCPUName(Name))
    return false;
  CPU = Name.str();
  return true;
}

bool PI32V2TargetInfo::hasFeature(StringRef Feature) const {
  return Feature == "pi32v2";
}

void PI32V2TargetInfo::getTargetDefines(const LangOptions &Opts,
                                        MacroBuilder &Builder) const {
  // JieLi's compiler defines __PI32V2__ (and __ELF__).
  Builder.defineMacro("__PI32V2__");
  Builder.defineMacro("__ELF__");
}
