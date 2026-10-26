//===-- PI32V2AsmBackend.cpp - PI32V2 assembler backend -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2FixupKinds.h"
#include "PI32V2MCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::PI32V2;

// What JieLi's compiler does with -mllvm -pi32v2-large-program=true (the AC79
// SDK passes it, to the linker's LTO too): code in flash and in RAM is further
// apart than call.l reaches (4 MiB), so a call the assembler leaves to the
// linker is the 6-byte call.xl, and calls in tail position are calls.
static cl::opt<bool> LargeProgram(
    "pi32v2-large-program", cl::init(false),
    cl::desc("Calls to symbols the assembler does not resolve are call.xl "
             "(32-bit reach), and no call becomes a tail call (JieLi's "
             "option of that name)"));

bool llvm::isPI32V2LargeProgram() { return LargeProgram; }

#define PI32V2_GEN_RELAX_TABLES
#include "PI32V2RelaxGen.inc"

static const PI32V2Relax *findRelax(unsigned Opcode) {
  for (const PI32V2Relax &R : Relaxations)
    if (R.From == Opcode)
      return &R;
  return nullptr;
}

namespace {
class PI32V2AsmBackend : public MCAsmBackend {
public:
  PI32V2AsmBackend() : MCAsmBackend(llvm::endianness::little) {}

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override;

  // JieLi's assembler resolves a branch to any label of the same section
  // that is not weak, global or not (docs/ELF.md section 5); LLVM's ELF
  // writer would leave a global one to the linker.
  std::optional<bool> evaluateFixup(const MCFragment &F, MCFixup &Fixup,
                                    MCValue &Target, uint64_t &Value) override {
    if (Fixup.getKind() < FirstTargetFixupKind || !Fixup.isPCRel())
      return {};
    const MCSymbol *A = Target.getAddSym();
    if (!A || Target.getSubSym())
      return {};
    int64_t C = Target.getConstant();
    // An alias (.set a2, tf+2): its value, if that is a label plus a number.
    for (int Depth = 0; A->isVariable() && Depth < 8; ++Depth) {
      if (static_cast<const MCSymbolELF *>(A)->getBinding() == ELF::STB_WEAK)
        return {};
      MCValue V;
      if (!A->getVariableValue()->evaluateAsRelocatable(V, Asm) ||
          !V.getAddSym() || V.getSubSym())
        return {};
      C += V.getConstant();
      A = V.getAddSym();
    }
    if (A->isVariable() || !A->isInSection())
      return {};
    if (static_cast<const MCSymbolELF *>(A)->getBinding() == ELF::STB_WEAK)
      return {};
    if (&A->getSection() != F.getParent())
      return {};
    uint64_t Sym;
    if (!Asm->getSymbolOffset(*A, Sym))
      return {};
    Value = Sym + C - (Asm->getFragmentOffset(F) + Fixup.getOffset());
    // During relaxation F's offset is already updated but a forward target's
    // is not: add the stretch, as MCAssembler does, or every instruction that
    // grew before F makes a short forward branch look out of reach.
    if (A->getFragment()->getLayoutOrder() > F.getLayoutOrder())
      Value += Asm->getStretch();
    return true;
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override;
  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;

  // A branch to a label starts in its smallest form; it grows to the next
  // form of its line while its target is out of reach, or while the target
  // needs a relocation the form does not have (pi32v2-isa docs/ELF.md,
  // "Branch reach").
  bool mayNeedRelaxation(unsigned Opcode, ArrayRef<MCOperand> Operands,
                         const MCSubtargetInfo &STI) const override {
    if (!findRelax(Opcode))
      return false;
    for (const MCOperand &MO : Operands)
      if (MO.isExpr())
        return true;
    return false;
  }
  bool fixupNeedsRelaxationAdvanced(const MCFragment &F, const MCFixup &Fixup,
                                    const MCValue &, uint64_t Value,
                                    bool Resolved) const override {
    unsigned Kind = Fixup.getKind();
    if (Kind < FirstTargetFixupKind)
      return false;
    const PI32V2FixupLayout &L = fixupLayout(Kind);
    if (!L.PCRel)
      return false;
    if (!Resolved)
      return L.Reloc == 0 ||
             (LargeProgram && F.getOpcode() == PI32V2::CALL_L);
    int64_t V = int64_t(Value) - L.Add;
    if (V % L.Scale)
      return false;
    return !isIntN(L.Width, V / L.Scale);
  }
  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override {
    const PI32V2Relax *R = findRelax(Inst.getOpcode());
    if (!R)
      return;
    MCInst Old = Inst;
    Inst.clear();
    Inst.setOpcode(R->To);
    Inst.setLoc(Old.getLoc());
    for (unsigned K = 0; K < R->ProgLen; ++K) {
      const PI32V2RelaxOp &Op = RelaxProg[R->ProgStart + K];
      if (Op.Kind == 0)
        Inst.addOperand(Old.getOperand(Op.Value));
      else if (Op.Kind == 1)
        Inst.addOperand(MCOperand::createReg(unsigned(Op.Value)));
      else
        Inst.addOperand(MCOperand::createImm(Op.Value));
    }
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createPI32V2ELFObjectWriter();
  }
};
} // namespace

MCFixupKindInfo PI32V2AsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);
  const PI32V2::PI32V2FixupLayout &L = PI32V2::fixupLayout(Kind);
  // The bits are not contiguous; applyFixup places them.
  return {L.Name, 0, unsigned(L.InsnBytes) * 8, 0};
}

void PI32V2AsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                  const MCValue &Target, uint8_t *Data,
                                  uint64_t Value, bool IsResolved) {
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);
  unsigned Kind = Fixup.getKind();
  if (Kind < FirstTargetFixupKind) {
    unsigned Size = getFixupKindInfo(Fixup.getKind()).TargetSize / 8;
    for (unsigned I = 0; I < Size; ++I)
      Data[I] |= uint8_t(Value >> (8 * I));
    return;
  }
  const PI32V2::PI32V2FixupLayout &L = PI32V2::fixupLayout(Kind);
  // JieLi's linker overwrites the field with the relocated value; the object
  // holds zero there unless the assembler resolved it.
  if (!IsResolved)
    return;
  int64_t V = int64_t(Value) - L.Add;
  if (V % L.Scale) {
    getContext().reportError(Fixup.getLoc(), L.PCRel ? "branch target is odd"
                                                     : "value is not a multiple "
                                                       "of the field's step");
    return;
  }
  V /= L.Scale;
  if (L.Width < 32 && (L.Signed ? !isIntN(L.Width, V) : !isUIntN(L.Width, V))) {
    getContext().reportError(Fixup.getLoc(), L.PCRel ? "branch target out of range"
                                                     : "value out of range");
    return;
  }
  unsigned Words = L.InsnBytes / 2;
  uint64_t Insn = 0;
  for (unsigned K = 0; K < Words; ++K)
    Insn = (Insn << 16) | Data[2 * K] | (uint64_t(Data[2 * K + 1]) << 8);
  for (unsigned K = 0; K < L.Width; ++K)
    if ((uint64_t(V) >> (L.Width - 1 - K)) & 1)
      Insn |= uint64_t(1) << L.Bits[K];
  for (unsigned K = 0; K < Words; ++K) {
    uint16_t W = Insn >> (16 * (Words - 1 - K));
    Data[2 * K] = W & 0xFF;
    Data[2 * K + 1] = W >> 8;
  }
}

// nop is 00 00.
bool PI32V2AsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                    const MCSubtargetInfo *STI) const {
  OS.write_zeros(Count);
  return true;
}

MCAsmBackend *llvm::createPI32V2AsmBackend(const Target &T,
                                           const MCSubtargetInfo &STI,
                                           const MCRegisterInfo &MRI,
                                           const MCTargetOptions &Options) {
  return new PI32V2AsmBackend();
}
