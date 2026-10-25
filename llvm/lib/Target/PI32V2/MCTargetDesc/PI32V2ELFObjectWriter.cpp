//===-- PI32V2ELFObjectWriter.cpp - PI32V2 ELF writer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// JieLi's relocations (pi32v2-isa docs/ELF.md section 5): RELA, at the first
// word of the instruction, naming the symbol itself.
//
//===----------------------------------------------------------------------===//

#include "PI32V2FixupKinds.h"
#include "PI32V2MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"

using namespace llvm;

namespace {
class PI32V2ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  PI32V2ELFObjectWriter()
      : MCELFObjectTargetWriter(/*Is64Bit=*/false, /*OSABI=*/0, ELF::EM_PI32V2,
                                /*HasRelocationAddend=*/true) {}

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override {
    unsigned Kind = Fixup.getKind();
    switch (Kind) {
    case FK_Data_1:
      return ELF::R_PI32V2_ABS8;
    case FK_Data_2:
      return ELF::R_PI32V2_ABS16;
    case FK_Data_4:
      return ELF::R_PI32V2_ABS32;
    default:
      break;
    }
    if (Kind >= FirstTargetFixupKind) {
      unsigned R = PI32V2::fixupLayout(Kind).Reloc;
      if (R)
        return R;
    }
    getContext().reportError(Fixup.getLoc(),
                             "this reference needs a longer instruction form");
    return ELF::R_PI32V2_NONE;
  }

  // Relocations name the symbol, never a section symbol (docs/ELF.md).
  bool needsRelocateWithSymbol(const MCValue &, unsigned Type) const override {
    return true;
  }
};
} // namespace

std::unique_ptr<MCObjectTargetWriter> llvm::createPI32V2ELFObjectWriter() {
  return std::make_unique<PI32V2ELFObjectWriter>();
}
