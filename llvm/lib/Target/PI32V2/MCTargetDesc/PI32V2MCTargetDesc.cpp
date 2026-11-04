//===-- PI32V2MCTargetDesc.cpp - PI32V2 target descriptions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2MCTargetDesc.h"
#include "PI32V2InstPrinter.h"
#include "PI32V2MCAsmInfo.h"
#include "TargetInfo/PI32V2TargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "PI32V2GenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "PI32V2GenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "PI32V2GenSubtargetInfo.inc"

using namespace llvm;

static MCAsmInfo *createPI32V2MCAsmInfo(const MCRegisterInfo &MRI,
                                        const Triple &TT,
                                        const MCTargetOptions &Options) {
  return new PI32V2MCAsmInfo(TT, Options);
}

namespace {
// .text is 2-aligned in JieLi's objects (pi32v2-isa docs/ELF.md section 2).
class PI32V2MCObjectFileInfo : public MCObjectFileInfo {
public:
  unsigned getTextSectionAlignment() const override { return 2; }
};
} // namespace

static MCObjectFileInfo *createPI32V2MCObjectFileInfo(MCContext &Ctx, bool PIC,
                                                      bool LargeCodeModel) {
  MCObjectFileInfo *MOFI = new PI32V2MCObjectFileInfo();
  MOFI->initMCObjectFileInfo(Ctx, PIC, LargeCodeModel);
  return MOFI;
}

static MCInstrInfo *createPI32V2MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitPI32V2MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createPI32V2MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitPI32V2MCRegisterInfo(X, PI32V2::RETS);
  return X;
}

static MCSubtargetInfo *
createPI32V2MCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  return createPI32V2MCSubtargetInfoImpl(TT, CPU, CPU, FS);
}

static MCInstPrinter *createPI32V2MCInstPrinter(const Triple &T,
                                                unsigned SyntaxVariant,
                                                const MCAsmInfo &MAI,
                                                const MCInstrInfo &MII,
                                                const MCRegisterInfo &MRI) {
  return new PI32V2InstPrinter(MAI, MII, MRI);
}

namespace {
class PI32V2MCInstrAnalysis : public MCInstrAnalysis {
public:
  explicit PI32V2MCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}

  // A branch's offset is in bytes from the end of the instruction (the operands
  // are scaled when decoded), and is the last operand, after the compared ones.
  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    const MCInstrDesc &Desc = Info->get(Inst.getOpcode());
    for (unsigned I = 0, E = std::min<unsigned>(Inst.getNumOperands(),
                                                Desc.getNumOperands());
         I != E; ++I)
      if (Desc.operands()[I].OperandType == MCOI::OPERAND_PCREL &&
          Inst.getOperand(I).isImm()) {
        Target = (Addr + Size + Inst.getOperand(I).getImm()) & 0xffffffff;
        return true;
      }
    return false;
  }
};
} // namespace

static MCInstrAnalysis *createPI32V2MCInstrAnalysis(const MCInstrInfo *Info) {
  return new PI32V2MCInstrAnalysis(Info);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializePI32V2TargetMC() {
  Target &T = getThePI32V2Target();
  TargetRegistry::RegisterMCAsmInfo(T, createPI32V2MCAsmInfo);
  TargetRegistry::RegisterMCObjectFileInfo(T, createPI32V2MCObjectFileInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createPI32V2MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createPI32V2MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createPI32V2MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createPI32V2MCInstPrinter);
  TargetRegistry::RegisterMCInstrAnalysis(T, createPI32V2MCInstrAnalysis);
  TargetRegistry::RegisterMCCodeEmitter(T, createPI32V2MCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createPI32V2AsmBackend);
}
