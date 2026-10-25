//===-- PI32V2TargetInfo.cpp - PI32V2 target info -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/PI32V2TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

Target &llvm::getThePI32V2Target() {
  static Target ThePI32V2Target;
  return ThePI32V2Target;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializePI32V2TargetInfo() {
  RegisterTarget<Triple::pi32v2> X(getThePI32V2Target(), "pi32v2",
                                   "JieLi pi32v2", "PI32V2");
}
