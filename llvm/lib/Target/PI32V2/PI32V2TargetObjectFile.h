//===-- PI32V2TargetObjectFile.h - PI32V2 object file lowering --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_PI32V2TARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_PI32V2_PI32V2TARGETOBJECTFILE_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {

/// Without function sections (the linker's LTO, see lld/ELF/LTO.cpp), the
/// functions share .text, so the assembler resolves the calls between them
/// and keeps the near ones in their 2-byte form. Only the functions that may
/// be dead get a section each, for --gc-sections: the library functions LTO
/// keeps whether or not code generation calls them (__udivsi3, memmove...)
/// and nothing else in the module references, and the local functions only
/// those reference.
class PI32V2TargetObjectFile : public TargetLoweringObjectFileELF {
  mutable const Module *Analyzed = nullptr;
  mutable DenseSet<const GlobalObject *> OwnSection;

  void analyze(const Module &M) const;

public:
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;
};

} // namespace llvm

#endif
