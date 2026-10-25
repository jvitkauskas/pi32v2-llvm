//===-- PI32V2IfBlocks.cpp - PI32V2 if (...) { } blocks -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An `if (cond) {` instruction runs the following 1..4 instructions only when
// its condition holds, and an optional else part of 0..3 instructions when it
// does not (pi32v2-isa docs/COMPILER.md, confirmed on the FM-1). JieLi's
// compiler uses them for short conditional code; this pass, the last before
// emission, does the same with the final layout:
//
//   B: if (c) goto T        B: if (!c) {
//   F: 1..4 instructions        F's instructions (the last may be a goto,
//   T: ...                      a call, a return)
//                             }
//                           T: ...
//
//   B: if (c) goto T        B: if (!c) {
//   F: 1..4 instructions        F's instructions
//      goto J               } else {
//   T: 1..3 instructions        T's instructions
//   J: ...                  }
//                           J: ...
//
// when F (and T) have no other predecessor. Every instruction in a part must
// be one machine instruction when emitted (the counts are in the opener).
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "pi32v2-if-blocks"

namespace {
class PI32V2IfBlocks : public MachineFunctionPass {
public:
  static char ID;
  PI32V2IfBlocks() : MachineFunctionPass(ID) {}
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "PI32V2 if blocks"; }
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

private:
  const PI32V2InstrInfo *TII = nullptr;
  bool convert(MachineBasicBlock &B);
};
} // namespace

char PI32V2IfBlocks::ID = 0;

FunctionPass *llvm::createPI32V2IfBlocksPass() { return new PI32V2IfBlocks(); }

// The block opener for a compare-and-branch: the same condition, the same
// operands.
unsigned PI32V2::openerForBranch(unsigned Br) {
  switch (Br) {
  case PI32V2::BEQ_RR: case PI32V2::BEQ_RR_L: return PI32V2::IF_EQ_RR;
  case PI32V2::BNE_RR: case PI32V2::BNE_RR_L: return PI32V2::IF_NE_RR;
  case PI32V2::BGE_RR: case PI32V2::BGE_RR_L: return PI32V2::IF_GE_RR;
  case PI32V2::BLT_RR: case PI32V2::BLT_RR_L: return PI32V2::IF_LT_RR;
  case PI32V2::BGT_RR: case PI32V2::BGT_RR_L: return PI32V2::IF_GT_RR;
  case PI32V2::BLE_RR: case PI32V2::BLE_RR_L: return PI32V2::IF_LE_RR;
  case PI32V2::BGES_RR: case PI32V2::BGES_RR_L: return PI32V2::IFS_GE_RR;
  case PI32V2::BLTS_RR: case PI32V2::BLTS_RR_L: return PI32V2::IFS_LT_RR;
  case PI32V2::BGTS_RR: case PI32V2::BGTS_RR_L: return PI32V2::IFS_GT_RR;
  case PI32V2::BLES_RR: case PI32V2::BLES_RR_L: return PI32V2::IFS_LE_RR;
  // the openers take 12-bit constants, the branches 10 or 12
  case PI32V2::BEQ_RI: case PI32V2::BEQ_RI_L: return PI32V2::IF_EQ_RI_S12;
  case PI32V2::BNE_RI: case PI32V2::BNE_RI_L: return PI32V2::IF_NE_RI_S12;
  case PI32V2::BGE_RI: case PI32V2::BGE_RI_L: return PI32V2::IF_GE_RI_S12;
  case PI32V2::BLT_RI: case PI32V2::BLT_RI_L: return PI32V2::IF_LT_RI_S12;
  case PI32V2::BGT_RI: case PI32V2::BGT_RI_L: return PI32V2::IF_GT_RI_S12;
  case PI32V2::BLE_RI: case PI32V2::BLE_RI_L: return PI32V2::IF_LE_RI_S12;
  case PI32V2::BGES_RI: case PI32V2::BGES_RI_L: return PI32V2::IFS_GE_RI_S12;
  case PI32V2::BLTS_RI: case PI32V2::BLTS_RI_L: return PI32V2::IFS_LT_RI_S12;
  case PI32V2::BGTS_RI: case PI32V2::BGTS_RI_L: return PI32V2::IFS_GT_RI_S12;
  case PI32V2::BLES_RI: case PI32V2::BLES_RI_L: return PI32V2::IFS_LE_RI_S12;
  case PI32V2::BFEQ_RR: case PI32V2::BFEQ_RR_L: return PI32V2::IFF_EQ_RR;
  case PI32V2::BFUNE_RR: case PI32V2::BFUNE_RR_L: return PI32V2::IFF_UNE_RR;
  case PI32V2::BFUGE_RR: case PI32V2::BFUGE_RR_L: return PI32V2::IFF_UGE_RR;
  case PI32V2::BFLT_RR: case PI32V2::BFLT_RR_L: return PI32V2::IFF_LT_RR;
  case PI32V2::BFUGT_RR: case PI32V2::BFUGT_RR_L: return PI32V2::IFF_UGT_RR;
  case PI32V2::BFLE_RR: case PI32V2::BFLE_RR_L: return PI32V2::IFF_LE_RR;
  case PI32V2::BFGE_RR: case PI32V2::BFGE_RR_L: return PI32V2::IFF_GE_RR;
  case PI32V2::BFULT_RR: case PI32V2::BFULT_RR_L: return PI32V2::IFF_ULT_RR;
  case PI32V2::BFGT_RR: case PI32V2::BFGT_RR_L: return PI32V2::IFF_GT_RR;
  case PI32V2::BFULE_RR: case PI32V2::BFULE_RR_L: return PI32V2::IFF_ULE_RR;
  // a bit (a single-bit mask is a modified immediate) or a mask
  case PI32V2::BEQ_AND_RI: case PI32V2::BEQ_AND_MODIMM_RI_L:
    return PI32V2::IF_EQ_AND_RM;
  case PI32V2::BNE_AND_RI: case PI32V2::BNE_AND_MODIMM_RI_L:
    return PI32V2::IF_NE_AND_RM;
  case PI32V2::BEQ_AND_RR: return PI32V2::IF_EQ_AND_RR;
  case PI32V2::BNE_AND_RR: return PI32V2::IF_NE_AND_RR;
  default: return 0;
  }
}

bool PI32V2::isIfOpener(unsigned Opc) {
  switch (Opc) {
  case PI32V2::IF_EQ_RR: case PI32V2::IF_NE_RR: case PI32V2::IF_GE_RR:
  case PI32V2::IF_LT_RR: case PI32V2::IF_GT_RR: case PI32V2::IF_LE_RR:
  case PI32V2::IFS_GE_RR: case PI32V2::IFS_LT_RR: case PI32V2::IFS_GT_RR:
  case PI32V2::IFS_LE_RR: case PI32V2::IF_EQ_RI_S12: case PI32V2::IF_NE_RI_S12:
  case PI32V2::IF_GE_RI_S12: case PI32V2::IF_LT_RI_S12:
  case PI32V2::IF_GT_RI_S12: case PI32V2::IF_LE_RI_S12:
  case PI32V2::IFS_GE_RI_S12: case PI32V2::IFS_LT_RI_S12:
  case PI32V2::IFS_GT_RI_S12: case PI32V2::IFS_LE_RI_S12:
  case PI32V2::IFF_EQ_RR: case PI32V2::IFF_UNE_RR: case PI32V2::IFF_UGE_RR:
  case PI32V2::IFF_LT_RR: case PI32V2::IFF_UGT_RR: case PI32V2::IFF_LE_RR:
  case PI32V2::IFF_GE_RR: case PI32V2::IFF_ULT_RR: case PI32V2::IFF_GT_RR:
  case PI32V2::IFF_ULE_RR: case PI32V2::IF_EQ_AND_RM: case PI32V2::IF_NE_AND_RM:
  case PI32V2::IF_EQ_AND_RR: case PI32V2::IF_NE_AND_RR:
    return true;
  default:
    return false;
  }
}

// A select is an if block of its own when emitted.
static bool isSelect(unsigned Opc) {
  return Opc == PI32V2::SELECT_AND || Opc == PI32V2::SELECT_ANDI ||
         Opc == PI32V2::SELECT_CC || Opc == PI32V2::SELECT_CC_RI ||
         Opc == PI32V2::SELECT_CC_RRI || Opc == PI32V2::SELECT_CC_RII;
}

// The instructions of a part: each emits exactly one instruction; only the
// last may leave (goto, return, tail call). NoExit: none may.
static bool partCount(MachineBasicBlock &MBB, bool AllowLastExit,
                      unsigned &N) {
  N = 0;
  MachineInstr *Last = nullptr;
  for (MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    if (MI.isMetaInstruction() || MI.isInlineAsm() || MI.isBundle() ||
        MI.isCFIInstruction() || MI.isConditionalBranch() ||
        PI32V2::isIfOpener(MI.getOpcode()) || isSelect(MI.getOpcode()))
      return false;
    if (Last && Last->isTerminator())
      return false; // something after a terminator
    Last = &MI;
    ++N;
  }
  if (Last && Last->isTerminator() && !AllowLastExit)
    return false;
  return true;
}

static MachineInstr *lastReal(MachineBasicBlock &MBB) {
  for (auto I = MBB.rbegin(); I != MBB.rend(); ++I)
    if (!I->isDebugInstr())
      return &*I;
  return nullptr;
}

static bool isGoto(const MachineInstr &MI) {
  return MI.getOpcode() == PI32V2::GOTO_S || MI.getOpcode() == PI32V2::GOTO_L;
}

static bool simplePred(MachineBasicBlock &X, MachineBasicBlock &B) {
  return X.pred_size() == 1 && *X.pred_begin() == &B && !X.hasAddressTaken() &&
         !X.isEHPad() && !X.isMachineBlockAddressTaken() &&
         X.getAlignment() <= Align(2);
}

bool PI32V2IfBlocks::convert(MachineBasicBlock &B) {
  MachineFunction &MF = *B.getParent();
  MachineBasicBlock::iterator BrI = B.getFirstTerminator();
  if (BrI == B.end() || std::next(BrI) != B.end())
    return false; // one terminator: the conditional branch
  MachineInstr &Br = *BrI;
  unsigned Inv = PI32V2InstrInfo::inverseBranch(Br.getOpcode());
  unsigned Opener = Inv ? PI32V2::openerForBranch(Inv) : 0;
  if (!Opener)
    return false;
  MachineBasicBlock *T = Br.getOperand(2).getMBB();
  auto FIt = std::next(B.getIterator());
  if (FIt == MF.end() || &*FIt == T)
    return false;
  MachineBasicBlock &F = *FIt;
  if (!simplePred(F, B)) {
    LLVM_DEBUG(dbgs() << "if blocks: " << printMBBReference(F)
                      << " has other predecessors\n");
    return false;
  }
  auto AfterF = std::next(F.getIterator());
  if (AfterF == MF.end() || &*AfterF != T) {
    LLVM_DEBUG(dbgs() << "if blocks: " << printMBBReference(F)
                      << " is not followed by the target\n");
    return false;
  }
  unsigned NF;
  if (!partCount(F, true, NF) || NF < 1) {
    LLVM_DEBUG(dbgs() << "if blocks: " << printMBBReference(F)
                      << " is no part\n");
    return false;
  }
  MachineInstr *FLast = lastReal(F);

  MachineBasicBlock *ElsePart = nullptr, *Join = nullptr;
  unsigned NT = 0;
  if (FLast->isTerminator() && isGoto(*FLast) && NF >= 2 && simplePred(*T, B)) {
    // the diamond: F jumps over T to J, T falls through to J
    MachineBasicBlock *J = FLast->getOperand(0).getMBB();
    auto AfterT = std::next(T->getIterator());
    if (AfterT != MF.end() && &*AfterT == J && T->succ_size() == 1 &&
        *T->succ_begin() == J && partCount(*T, false, NT) && NT >= 1 &&
        NT <= 3 && NF - 1 <= 4) {
      ElsePart = T;
      Join = J;
    }
  }
  if (!ElsePart && NF > 4)
    return false;
  if (!ElsePart && !FLast->isTerminator() &&
      !(F.succ_size() == 1 && *F.succ_begin() == T))
    return false; // F must fall through to T, or leave

  // the opener, with the counts
  DebugLoc DL = Br.getDebugLoc();
  unsigned Then = ElsePart ? NF - 1 : NF;
  BuildMI(B, BrI, DL, TII->get(Opener))
      .add(Br.getOperand(0))
      .add(Br.getOperand(1))
      .addImm(Then)
      .addImm(NT);
  Br.eraseFromParent();
  if (ElsePart)
    FLast->eraseFromParent(); // the goto over the else part
  B.splice(B.end(), &F, F.begin(), F.end());
  if (ElsePart)
    B.splice(B.end(), ElsePart, ElsePart->begin(), ElsePart->end());

  // successors
  B.removeSuccessor(&F);
  if (ElsePart) {
    B.removeSuccessor(ElsePart);
    if (!B.isSuccessor(Join))
      B.addSuccessor(Join);
  } else {
    for (MachineBasicBlock *S : F.successors())
      if (!B.isSuccessor(S))
        B.addSuccessor(S);
  }
  for (MachineBasicBlock *X : {&F, ElsePart}) {
    if (!X)
      continue;
    while (!X->succ_empty())
      X->removeSuccessor(X->succ_begin());
    X->eraseFromParent();
  }
  return true;
}

bool PI32V2IfBlocks::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;
  TII = MF.getSubtarget<PI32V2Subtarget>().getInstrInfo();
  bool Changed = false;
  for (auto I = MF.begin(); I != MF.end(); ++I)
    while (convert(*I))
      Changed = true;
  return Changed;
}
