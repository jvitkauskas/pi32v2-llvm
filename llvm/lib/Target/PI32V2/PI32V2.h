//===-- PI32V2.h - Top-level interface for PI32V2 ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_PI32V2_H
#define LLVM_LIB_TARGET_PI32V2_PI32V2_H

#include "llvm/Support/CodeGen.h"

namespace llvm {
class FunctionPass;
class PassRegistry;
class PI32V2TargetMachine;

FunctionPass *createPI32V2ISelDag(PI32V2TargetMachine &TM,
                                  CodeGenOptLevel OptLevel);
void initializePI32V2AsmPrinterPass(PassRegistry &);
void initializePI32V2DAGToDAGISelLegacyPass(PassRegistry &);
} // namespace llvm

#endif
