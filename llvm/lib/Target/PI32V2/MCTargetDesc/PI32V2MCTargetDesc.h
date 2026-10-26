//===-- PI32V2MCTargetDesc.h - PI32V2 target descriptions -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_MCTARGETDESC_PI32V2MCTARGETDESC_H
#define LLVM_LIB_TARGET_PI32V2_MCTARGETDESC_PI32V2MCTARGETDESC_H

#include "llvm/MC/MCTargetOptions.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class Target;

MCCodeEmitter *createPI32V2MCCodeEmitter(const MCInstrInfo &MCII,
                                         MCContext &Ctx);
MCAsmBackend *createPI32V2AsmBackend(const Target &T,
                                     const MCSubtargetInfo &STI,
                                     const MCRegisterInfo &MRI,
                                     const MCTargetOptions &Options);
std::unique_ptr<MCObjectTargetWriter> createPI32V2ELFObjectWriter();

// JieLi's -pi32v2-large-program: a call the assembler cannot resolve is the
// 6-byte call.xl (any distance), and there are no tail calls.
bool isPI32V2LargeProgram();
} // namespace llvm

#define GET_REGINFO_ENUM
#include "PI32V2GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "PI32V2GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "PI32V2GenSubtargetInfo.inc"

#endif
