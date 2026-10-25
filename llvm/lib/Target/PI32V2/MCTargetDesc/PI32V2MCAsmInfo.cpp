//===-- PI32V2MCAsmInfo.cpp - PI32V2 assembly properties ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2MCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

PI32V2MCAsmInfo::PI32V2MCAsmInfo(const Triple &TT,
                                 const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 4;
  CalleeSaveStackSlotSize = 4;
  IsLittleEndian = true;
  // JieLi's assembler: ';' and '//' start a comment; '#' marks a parallel pair
  // (pi32v2-isa docs/ASSEMBLER.md).
  CommentString = ";";
  InternalSymbolPrefix = ".L";
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
  UsesELFSectionDirectiveForBSS = true;
  MinInstAlignment = 2;
  AllowAtInName = true;
  UseAtForSpecifier = true; // lets the lexer keep @ in names (_x@y:)
}
