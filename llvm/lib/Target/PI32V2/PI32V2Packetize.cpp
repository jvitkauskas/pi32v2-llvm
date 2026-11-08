//===-- PI32V2Packetize.cpp - PI32V2 parallel-issue pairs -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The emitter issues an instruction with a # twin in parallel with the short
// load or store after it (MCTargetDesc/PI32V2Parallel.cpp). This pass, the
// last before emission, puts such instructions there: for each short load or
// store that has none before it, an independent one from a few instructions
// before or after it moves next to it, as JieLi's compiler arranges its code.
// Only registers order the moved instruction (it does not touch memory), and
// the flags it sets must be dead where it goes. Calls, if blocks and anything
// else not plain stay where they are, and nothing moves across them.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PI32V2BaseInfo.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "pi32v2-packetize"

static cl::opt<bool> EnablePacketize(
    "pi32v2-packetize", cl::Hidden, cl::init(true),
    cl::desc("PI32V2: move instructions next to the short loads and stores "
             "they can be issued in parallel with"));

static cl::opt<unsigned> Window("pi32v2-packetize-window", cl::Hidden,
                                cl::init(8),
                                cl::desc("PI32V2: how far an instruction moves "
                                         "for parallel issue"));

namespace {
class PI32V2Packetize : public MachineFunctionPass {
public:
  static char ID;
  PI32V2Packetize() : MachineFunctionPass(ID) {
    initializePI32V2PacketizePass(*PassRegistry::getPassRegistry());
  }
  StringRef getPassName() const override {
    return "PI32V2 parallel-issue pairs";
  }
  bool runOnMachineFunction(MachineFunction &MF) override;
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

private:
  const PI32V2InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  bool lower(const MachineInstr &MI, MCInst &Inst) const;
  bool runOnBlock(MachineBasicBlock &MBB);
};
} // namespace

char PI32V2Packetize::ID = 0;

INITIALIZE_PASS(PI32V2Packetize, DEBUG_TYPE, "PI32V2 parallel-issue pairs",
                false, false)

FunctionPass *llvm::createPI32V2PacketizePass() { return new PI32V2Packetize(); }

// MI as the emitter makes it (registers and constants only; any other operand:
// false, not a candidate).
bool PI32V2Packetize::lower(const MachineInstr &MI, MCInst &Inst) const {
  Inst = MCInst();
  unsigned Skip;
  unsigned Opc = MI.getOpcode();
  if (unsigned Form = PI32V2InstrInfo::postIncForm(Opc, Skip))
    Opc = Form;
  else if (MI.isPseudo() || MI.isCall() || MI.isReturn() || MI.isBranch() ||
           MI.isInlineAsm())
    return false;
  else
    Skip = ~0u;
  Inst.setOpcode(Opc);
  for (unsigned K = 0; K < MI.getNumOperands(); ++K) {
    const MachineOperand &MO = MI.getOperand(K);
    if (K == Skip || (MO.isReg() && MO.isImplicit()))
      continue;
    if (MO.isReg())
      Inst.addOperand(MCOperand::createReg(MO.getReg()));
    else if (MO.isImm())
      Inst.addOperand(MCOperand::createImm(MO.getImm()));
    else
      return false;
  }
  PI32V2::compressInst(Inst, *TII, *TRI);
  return true;
}

static bool touches(const MachineInstr &MI, MCRegister R,
                    const TargetRegisterInfo *TRI, bool Defs, bool Uses) {
  for (const MachineOperand &MO : MI.operands())
    if (MO.isReg() && MO.getReg() && TRI->regsOverlap(MO.getReg(), R) &&
        ((Defs && MO.isDef()) || (Uses && MO.readsReg())))
      return true;
  return false;
}

// Whether A can be moved past MI (either way): no register both write, or one
// writes and the other reads. The flags A sets (dead) are left to flagsLive.
static bool independent(const MachineInstr &A, const MachineInstr &MI,
                        const TargetRegisterInfo *TRI) {
  for (const MachineOperand &MO : A.operands()) {
    if (!MO.isReg() || !MO.getReg() || MO.getReg() == PI32V2::PSR)
      continue;
    if (MO.isDef() && touches(MI, MO.getReg(), TRI, true, true))
      return false;
    if (MO.readsReg() && touches(MI, MO.getReg(), TRI, true, false))
      return false;
  }
  return true;
}

// Whether the flags are read from It on before they are written again.
static bool flagsLive(MachineBasicBlock::iterator It, MachineBasicBlock &MBB,
                      const TargetRegisterInfo *TRI) {
  for (; It != MBB.end(); ++It) {
    if (It->isDebugInstr())
      continue;
    if (touches(*It, PI32V2::PSR, TRI, false, true))
      return true;
    if (touches(*It, PI32V2::PSR, TRI, true, false))
      return false;
  }
  for (MachineBasicBlock *S : MBB.successors())
    if (S->isLiveIn(PI32V2::PSR))
      return true;
  return false;
}

bool PI32V2Packetize::runOnBlock(MachineBasicBlock &MBB) {
  // the instructions that can move or be passed: plain ones outside if blocks
  SmallVector<MachineInstr *, 64> Seq;
  SmallVector<bool, 64> Fixed;
  unsigned InIf = 0;
  for (MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    bool F = InIf > 0;
    if (InIf)
      --InIf;
    if (PI32V2::isIfOpener(MI.getOpcode())) {
      InIf = MI.getOperand(2).getImm() + MI.getOperand(3).getImm();
      F = true;
    }
    unsigned Skip;
    F |= MI.isCall() || MI.isTerminator() || MI.isInlineAsm() ||
         (MI.isPseudo() && !PI32V2InstrInfo::postIncForm(MI.getOpcode(), Skip)) ||
         MI.isBundled() || MI.isKill() || MI.isImplicitDef() ||
         MI.isCFIInstruction() || MI.isLabel();
    Seq.push_back(&MI);
    Fixed.push_back(F);
  }
  const MCInstrInfo &MII = *TII;
  const MCRegisterInfo &MRI = *TRI;
  SmallPtrSet<const MachineInstr *, 32> Used;
  bool Changed = false;

  auto IsA = [&](const MachineInstr *MI, MCInst &Inst) {
    if (!lower(*MI, Inst) || !PI32V2::parallelTwin(Inst.getOpcode(), MII))
      return false;
    for (const MachineOperand &MO : MI->operands())
      if (MO.isReg() && MO.isDef() && MO.getReg() == PI32V2::PSR && !MO.isDead())
        return false;
    for (const MachineOperand &MO : MI->operands())
      if (MO.isReg() && MO.readsReg() && MO.getReg() == PI32V2::PSR)
        return false;
    return !MI->mayLoadOrStore();
  };

  for (unsigned J = 0; J < Seq.size(); ++J) {
    MachineInstr *B = Seq[J];
    MCInst BI, AI;
    if (Fixed[J] || Used.count(B) || !lower(*B, BI) ||
        !PI32V2::isParallelB(BI, MRI))
      continue;
    // already one
    if (J > 0 && !Fixed[J - 1] && !Used.count(Seq[J - 1]) &&
        IsA(Seq[J - 1], AI) && PI32V2::canIssueParallel(AI, BI, MII, MRI)) {
      Used.insert(Seq[J - 1]);
      Used.insert(B);
      continue;
    }
    // one from before B, moved down to it
    unsigned Found = ~0u;
    for (unsigned K = 2; K <= Window && K <= J; ++K) {
      unsigned I = J - K;
      MachineInstr *A = Seq[I];
      if (Fixed[I])
        break;
      if (Used.count(A) || !IsA(A, AI) ||
          !PI32V2::canIssueParallel(AI, BI, MII, MRI))
        continue;
      bool Ok = true;
      for (unsigned M = I + 1; M < J && Ok; ++M)
        Ok = !Fixed[M] && !Used.count(Seq[M]) && independent(*A, *Seq[M], TRI);
      // (B too may not read or write what A writes: canIssueParallel)
      if (Ok && touches(*A, PI32V2::PSR, TRI, true, false) &&
          flagsLive(B->getIterator(), MBB, TRI))
        Ok = false;
      if (Ok) {
        Found = I;
        break;
      }
    }
    if (Found != ~0u) {
      MachineInstr *A = Seq[Found];
      for (unsigned M = Found + 1; M < J; ++M)
        for (MachineOperand &MO : A->uses())
          if (MO.isReg() && MO.getReg())
            Seq[M]->clearRegisterKills(MO.getReg(), TRI);
      MBB.splice(B->getIterator(), &MBB, A->getIterator());
      Seq.erase(Seq.begin() + Found);
      Fixed.erase(Fixed.begin() + Found);
      --J; // B moved one place back
      Used.insert(A);
      Used.insert(B);
      Changed = true;
      continue;
    }
    // one from after B, moved up to it: A comes before B, so B must not write
    // what A reads either
    for (unsigned K = 1; K <= Window && J + K < Seq.size(); ++K) {
      unsigned I = J + K;
      MachineInstr *A = Seq[I];
      if (Fixed[I])
        break;
      if (Used.count(A) || !IsA(A, AI) ||
          !PI32V2::canIssueParallel(AI, BI, MII, MRI) ||
          !independent(*A, *B, TRI))
        continue;
      bool Ok = true;
      for (unsigned M = J + 1; M < I && Ok; ++M)
        Ok = !Fixed[M] && !Used.count(Seq[M]) && independent(*A, *Seq[M], TRI);
      // (A's flags, dead, now come before those it passed: none may read them)
      if (Ok && touches(*A, PI32V2::PSR, TRI, true, false))
        for (unsigned M = J; M < I && Ok; ++M)
          Ok = !touches(*Seq[M], PI32V2::PSR, TRI, false, true);
      if (!Ok)
        continue;
      // A now reads before those it passed: a register they read too is no
      // longer killed by A, and one they kill is killed by them still
      for (MachineOperand &MO : A->uses())
        if (MO.isReg() && MO.isKill())
          for (unsigned M = J; M < I; ++M)
            if (touches(*Seq[M], MO.getReg(), TRI, false, true))
              MO.setIsKill(false);
      MBB.splice(B->getIterator(), &MBB, A->getIterator());
      Seq.erase(Seq.begin() + I);
      Fixed.erase(Fixed.begin() + I);
      Seq.insert(Seq.begin() + J, A);
      Fixed.insert(Fixed.begin() + J, false);
      ++J; // B is one place on
      Used.insert(A);
      Used.insert(B);
      Changed = true;
      break;
    }
  }
  return Changed;
}

bool PI32V2Packetize::runOnMachineFunction(MachineFunction &MF) {
  if (!EnablePacketize || skipFunction(MF.getFunction()))
    return false;
  TII = MF.getSubtarget<PI32V2Subtarget>().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    Changed |= runOnBlock(MBB);
  return Changed;
}
