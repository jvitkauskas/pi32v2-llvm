//===-- PI32V2FreezeReturns.h - A missing return is a value -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_PI32V2FREEZERETURNS_H
#define LLVM_LIB_TARGET_PI32V2_PI32V2FREEZERETURNS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// The undef a function returns where it has no return statement becomes a
/// frozen value (PI32V2FreezeReturns.cpp).
struct PI32V2FreezeReturnsPass : PassInfoMixin<PI32V2FreezeReturnsPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif
