//===-- PI32V2AnchorBase.cpp - PI32V2 base register anchors ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The 16-bit loads and stores reach 5 bits of offset in units of the access
// size (words -64..60, halfwords -32..30, bytes -16..15); every access farther
// from its base is 4 bytes. A run of accesses at, say, [r0+120] .. [r0+132]
// becomes
//
//   r1 = [++r0=120]      (the first access, which steps the base to its offset)
//   r2 = [r0+4]          (the others, in 16 bits)
//   ...
//
// as JieLi's compiler does it. When the base is needed afterwards the
// pre-increment costs a copy of it, which the saving has to cover. Before
// register allocation, on the selected accesses of one block.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2Subtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "pi32v2-anchor-base"

static cl::opt<bool> EnableAnchor("pi32v2-anchor-base", cl::Hidden, cl::init(true),
                                  cl::desc("Anchor far accesses with a pre-increment"));

static cl::opt<bool> EnableShare("pi32v2-share-far-base", cl::Hidden, cl::init(true),
                                 cl::desc("Share the address add of far accesses across blocks"));

namespace {
class PI32V2AnchorBase : public MachineFunctionPass {
public:
  static char ID;
  PI32V2AnchorBase() : MachineFunctionPass(ID) {}
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "PI32V2 base register anchors"; }

private:
  struct Access {
    MachineInstr *MI;
    Register Base;
    int64_t Off;
    unsigned Size;
    bool Short; // has a 16-bit form (an unsigned load, a store)
    bool Store;
    unsigned Pre; // the pre-increment pseudo
    // The access is [t+Off] with t = Base' + n made by Via in this block for
    // this access alone (Base and Off are then Base' and n + Off): the far
    // address that CodeGenPrepare left to one instruction each.
    MachineInstr *Via = nullptr;
  };
  bool describe(MachineInstr &MI, Access &A, bool Fold = true) const;
  bool convert(MachineBasicBlock &B);
  bool shareAcrossBlocks(MachineFunction &MF);
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  MachineDominatorTree *MDT = nullptr;
  MachineBlockFrequencyInfo *MBFI = nullptr;
  MachineLoopInfo *MLI = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  const PI32V2InstrInfo *TII = nullptr;
};
} // namespace

char PI32V2AnchorBase::ID = 0;

FunctionPass *llvm::createPI32V2AnchorBasePass() { return new PI32V2AnchorBase(); }

bool PI32V2AnchorBase::describe(MachineInstr &MI, Access &A, bool Fold) const {
  A.MI = &MI;
  A.Short = true;
  A.Store = false;
  unsigned BaseIdx = 1, OffIdx = 2;
  switch (MI.getOpcode()) {
  case PI32V2::LD_W_L: A.Size = 4; A.Pre = PI32V2::LDW_PRE; break;
  case PI32V2::LD_HU_L: A.Size = 2; A.Pre = PI32V2::LDHU_PRE; break;
  case PI32V2::LD_BU_L: A.Size = 1; A.Pre = PI32V2::LDBU_PRE; break;
  case PI32V2::LD_HS_L: A.Size = 2; A.Pre = PI32V2::LDHS_PRE; A.Short = false; break;
  case PI32V2::LD_BS_L: A.Size = 1; A.Pre = PI32V2::LDBS_PRE; A.Short = false; break;
  case PI32V2::ST_W_L: A.Size = 4; A.Pre = PI32V2::STW_PRE; A.Store = true; break;
  case PI32V2::ST_H_L: A.Size = 2; A.Pre = PI32V2::STH_PRE; A.Store = true; break;
  case PI32V2::ST_B_L: A.Size = 1; A.Pre = PI32V2::STB_PRE; A.Store = true; break;
  default: return false;
  }
  if (A.Store) {
    BaseIdx = 0;
    OffIdx = 1;
  }
  if (!MI.getOperand(BaseIdx).isReg() || !MI.getOperand(OffIdx).isImm() ||
      !MI.getOperand(BaseIdx).getReg().isVirtual())
    return false;
  A.Base = MI.getOperand(BaseIdx).getReg();
  A.Off = MI.getOperand(OffIdx).getImm();
  A.Via = nullptr;
  MachineInstr *Def = MRI->getUniqueVRegDef(A.Base);
  if (Fold && Def && Def->getOpcode() == PI32V2::ADD_S14_RRI &&
      Def->getParent() == MI.getParent() &&
      Def->getOperand(1).getReg().isVirtual() && Def->getOperand(2).isImm() &&
      MRI->hasOneNonDBGUse(A.Base)) {
    A.Via = Def;
    A.Base = Def->getOperand(1).getReg();
    A.Off += Def->getOperand(2).getImm();
  }
  return true;
}

// the 16-bit forms' offset range for an access of Size bytes
static bool fitsShort(int64_t Off, unsigned Size) {
  return Off % Size == 0 && isInt<5>(Off / int64_t(Size));
}

// the pre-increment's offset range (the step to the anchor)
static bool fitsPre(int64_t Off, unsigned Size) {
  return Off % Size == 0 && isInt<9>(Off / int64_t(Size));
}

bool PI32V2AnchorBase::convert(MachineBasicBlock &B) {
  SmallVector<Access, 32> Acc;
  DenseMap<MachineInstr *, unsigned> Pos;
  for (MachineInstr &MI : B) {
    Pos[&MI] = Pos.size();
    Access A;
    if (describe(MI, A))
      Acc.push_back(A);
  }
  bool Changed = false;
  SmallPtrSet<MachineInstr *, 16> Done;
  for (unsigned I = 0; I < Acc.size(); ++I) {
    Access &First = Acc[I];
    if (Done.count(First.MI) || fitsShort(First.Off, First.Size) ||
        !fitsPre(First.Off, First.Size))
      continue; // the anchor is a 32-bit access anyway
    // the later accesses on this base that the step brings in range
    SmallVector<unsigned, 8> Members;
    for (unsigned J = I + 1; J < Acc.size(); ++J) {
      Access &A = Acc[J];
      if (A.Base != First.Base || Done.count(A.MI) || !A.Short ||
          fitsShort(A.Off, A.Size))
        continue;
      if (fitsShort(A.Off - First.Off, A.Size))
        Members.push_back(J);
    }
    if (Members.empty())
      continue;
    // what the group saves: a member gets 2 bytes shorter, or loses its add
    // (4 bytes); the anchor loses its add (2 net: the pre-increment is 4)
    int Saved = First.Via ? 2 : 0;
    SmallPtrSet<MachineInstr *, 16> Inside;
    Inside.insert(First.MI);
    if (First.Via)
      Inside.insert(First.Via);
    for (unsigned J : Members) {
      Saved += Acc[J].Via ? 4 : 2;
      Inside.insert(Acc[J].MI);
      if (Acc[J].Via)
        Inside.insert(Acc[J].Via);
    }
    // The base after the anchor: used by anything else than the group? (A use
    // before the anchor, in this block, does not keep it alive.)
    bool Live = false;
    for (MachineInstr &U : MRI->use_nodbg_instructions(First.Base)) {
      if (Inside.count(&U))
        continue;
      if (U.getParent() != &B || Pos[&U] > Pos[First.MI]) {
        Live = true;
        break;
      }
    }
    if (Live)
      Saved -= 2;
    if (Saved < 2)
      continue;
    // convert
    MachineInstr &F = *First.MI;
    DebugLoc DL = F.getDebugLoc();
    Register Wb = MRI->createVirtualRegister(&PI32V2::GPRRegClass);
    MachineInstrBuilder MIB;
    if (First.Store) {
      MIB = BuildMI(B, F, DL, TII->get(First.Pre), Wb)
                .addReg(First.Base)
                .addImm(First.Off)
                .add(F.getOperand(2));
    } else {
      MIB = BuildMI(B, F, DL, TII->get(First.Pre), F.getOperand(0).getReg())
                .addReg(Wb, RegState::Define)
                .addReg(First.Base)
                .addImm(First.Off);
    }
    MIB.cloneMemRefs(F);
    F.eraseFromParent();
    if (First.Via)
      First.Via->eraseFromParent();
    Done.insert(MIB.getInstr());
    for (unsigned J : Members) {
      MachineInstr &M = *Acc[J].MI;
      unsigned BaseIdx = Acc[J].Store ? 0 : 1;
      M.getOperand(BaseIdx).setReg(Wb);
      M.getOperand(BaseIdx + 1).setImm(Acc[J].Off - First.Off);
      if (Acc[J].Via)
        Acc[J].Via->eraseFromParent();
      Done.insert(&M);
    }
    Changed = true;
  }

  // The far accesses the step cannot reach (the anchor must be a 32-bit
  // access: past its 9 bits) share adds: t = base + n made once, then [t+k]
  // for the neighbours within the offset range of the access, instead of an
  // add each; a few such bases are live at a time, as JieLi's compiler does.
  constexpr unsigned Window = 40; // (instructions after its last use)
  constexpr unsigned MaxLive = 5;
  struct Anchor {
    Register T, Base;
    int64_t Imm;
    unsigned Last;
  };
  SmallVector<Anchor, MaxLive + 1> Live;
  for (Access &A : Acc) {
    if (!A.Via || Done.count(A.MI))
      continue;
    unsigned P = Pos[A.MI];
    llvm::erase_if(Live, [&](Anchor &L) { return P - L.Last > Window; });
    Anchor *Best = nullptr;
    int BestRank = 0;
    for (Anchor &L : Live) {
      int64_t Rel = A.Off - L.Imm;
      if (L.Base != A.Base || !fitsPre(Rel, A.Size))
        continue;
      int Rank = fitsShort(Rel, A.Size) && A.Short ? 2 : 1;
      if (Rank > BestRank) {
        Best = &L;
        BestRank = Rank;
      }
    }
    if (Best) {
      unsigned BaseIdx = A.Store ? 0 : 1;
      A.MI->getOperand(BaseIdx).setReg(Best->T);
      A.MI->getOperand(BaseIdx + 1).setImm(A.Off - Best->Imm);
      A.Via->eraseFromParent();
      A.Via = nullptr;
      Best->Last = P;
      Changed = true;
      continue;
    }
    if (Live.size() == MaxLive)
      Live.erase(std::min_element(Live.begin(), Live.end(),
                                  [](Anchor &X, Anchor &Y) { return X.Last < Y.Last; }));
    Live.push_back({A.Via->getOperand(0).getReg(), A.Base,
                    A.Via->getOperand(2).getImm(), P});
  }
  return Changed;
}

// Whether the condition register is live at I (read before it is written).
static bool psrLiveAt(MachineBasicBlock::iterator I, MachineBasicBlock &B) {
  for (; I != B.end(); ++I) {
    if (I->readsRegister(PI32V2::PSR, nullptr) && !I->isDebugInstr())
      return true;
    if (I->modifiesRegister(PI32V2::PSR, nullptr))
      return false;
  }
  return false;
}

// Far accesses to one base share the add of one of them when it can be placed
// in a block that dominates them all and runs no more often than the adds did:
// t = base + n once (JieLi's compiler keeps such pointers in registers), then
// [t+k], 16 bits near it and 32 bits within 9 bits of it. An address that is
// loaded and stored (x += ...) has one add for both.
bool PI32V2AnchorBase::shareAcrossBlocks(MachineFunction &MF) {
  struct Unit {
    MachineInstr *Add;
    int64_t Imm;
    SmallVector<Access, 2> Users; // Off: from the add's base
    bool Taken = false;
    // what the unit's users cost when [t+Rel]: -1 if they cannot reach
    int gain(int64_t Anchor) const {
      int G = 4;
      for (const Access &U : Users) {
        int64_t Rel = Imm + U.Off - Anchor;
        if (!fitsPre(Rel, U.Size))
          return -1;
        if (U.Short && !fitsShort(Rel, U.Size))
          G -= 2;
      }
      return G;
    }
  };
  DenseMap<Register, SmallVector<Unit, 16>> ByBase;
  for (MachineBasicBlock &B : MF)
    for (MachineInstr &MI : B) {
      if (MI.getOpcode() != PI32V2::ADD_S14_RRI || !MI.getOperand(1).isReg() ||
          !MI.getOperand(1).getReg().isVirtual() || !MI.getOperand(2).isImm())
        continue;
      Register T = MI.getOperand(0).getReg();
      Unit U;
      U.Add = &MI;
      U.Imm = MI.getOperand(2).getImm();
      bool Ok = !MRI->use_nodbg_empty(T) && T != MI.getOperand(1).getReg();
      for (MachineOperand &Op : MRI->use_nodbg_operands(T)) {
        Access A;
        MachineInstr &User = *Op.getParent();
        if (!describe(User, A, /*Fold=*/false) || A.Base != T ||
            User.getOperand(A.Store ? 0 : 1).getReg() != T ||
            (A.Store && User.getOperand(2).isReg() &&
             User.getOperand(2).getReg() == T)) {
          Ok = false;
          break;
        }
        U.Users.push_back(A);
      }
      if (Ok)
        ByBase[MI.getOperand(1).getReg()].push_back(std::move(U));
    }
  bool Changed = false;
  for (auto &[Base, Units] : ByBase) {
    if (Units.size() < 2)
      continue;
    while (true) {
      // the anchor whose neighbourhood saves most
      int BestGain = 0;
      Unit *Best = nullptr;
      for (Unit &C : Units) {
        if (C.Taken)
          continue;
        int Gain = 0;
        for (Unit &M : Units)
          if (!M.Taken && &M != &C)
            Gain += std::max(0, M.gain(C.Imm));
        if (Gain > BestGain) {
          BestGain = Gain;
          Best = &C;
        }
      }
      if (!Best || BestGain < 2)
        break;
      SmallVector<Unit *, 8> Members;
      for (Unit &M : Units)
        if (!M.Taken && &M != Best && M.gain(Best->Imm) > 0)
          Members.push_back(&M);
      Members.push_back(Best);
      for (Unit *M : Members)
        M->Taken = true;
      // where: a block dominating the users, run no more often than the adds
      MachineBasicBlock *D = Best->Add->getParent();
      BlockFrequency Total;
      for (Unit *M : Members) {
        Total += MBFI->getBlockFreq(M->Add->getParent());
        for (Access &U : M->Users)
          D = MDT->findNearestCommonDominator(D, U.MI->getParent());
      }
      if (!D || MBFI->getBlockFreq(D) > Total)
        continue;
      // out of the loops, where the base allows: the add is made once
      MachineBasicBlock *Def = MRI->getVRegDef(Base)->getParent();
      if (MachineLoop *L = MLI->getLoopFor(D)) {
        while (L->getParentLoop())
          L = L->getParentLoop();
        MachineBasicBlock *P = L->getLoopPreheader();
        if (!P)
          if (auto *N = MDT->getNode(L->getHeader())->getIDom())
            P = N->getBlock();
        if (P && !L->contains(P) && MDT->dominates(Def, P))
          D = P;
      }
      MachineBasicBlock::iterator At = D->getFirstTerminator();
      for (MachineInstr &MI : *D) {
        bool Uses = false;
        for (Unit *M : Members)
          for (Access &U : M->Users)
            Uses |= U.MI == &MI;
        if (Uses) {
          At = MachineBasicBlock::iterator(MI);
          break;
        }
      }
      if (psrLiveAt(At, *D))
        continue;
      // (a block that is the base's own: the add goes after its definition)
      if (D == Def && MRI->getVRegDef(Base)->getParent() == D) {
        bool After = false;
        for (MachineBasicBlock::iterator I = At; I != D->end(); ++I)
          After |= &*I == MRI->getVRegDef(Base);
        if (After)
          continue;
      }
      D->splice(At, Best->Add->getParent(), Best->Add);
      Register T = Best->Add->getOperand(0).getReg();
      for (Unit *M : Members) {
        if (M == Best)
          continue;
        for (Access &U : M->Users) {
          unsigned BaseIdx = U.Store ? 0 : 1;
          U.MI->getOperand(BaseIdx).setReg(T);
          U.MI->getOperand(BaseIdx + 1).setImm(M->Imm + U.Off - Best->Imm);
        }
        M->Add->eraseFromParent();
      }
      Changed = true;
    }
  }
  return Changed;
}

bool PI32V2AnchorBase::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableAnchor || skipFunction(MF.getFunction()))
    return false;
  const auto &ST = MF.getSubtarget<PI32V2Subtarget>();
  TII = ST.getInstrInfo();
  MRI = &MF.getRegInfo();
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  MBFI = &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  bool Changed = false;
  if (EnableShare)
    Changed |= shareAcrossBlocks(MF);
  for (MachineBasicBlock &B : MF)
    Changed |= convert(B);
  return Changed;
}
