//===-- PI32V2TargetMachine.cpp - PI32V2 target machine -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// No code generator yet: the target has its machine-code layer (assembler,
// disassembler, object files) only.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Compiler.h"

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializePI32V2Target() {}
