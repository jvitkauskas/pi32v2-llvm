//===-- PI32V2TargetObjectFile.cpp - PI32V2 object file lowering ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2TargetObjectFile.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

static cl::opt<bool> SharedText(
    "pi32v2-shared-text", cl::init(true), cl::Hidden,
    cl::desc("Without function sections, put the functions that are not "
             "possibly dead in one .text (false: a section each, as with "
             "function sections; for the linker's LTO: "
             "-mllvm -pi32v2-shared-text=false)"));

void PI32V2TargetObjectFile::Initialize(MCContext &Ctx,
                                        const TargetMachine &TM) {
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);
  Analyzed = nullptr;
  OwnSection.clear();
}

// The functions that may be dead after code generation: the library functions
// LTO keeps for code generation's sake (LTO.cpp, isLibcall), and the local
// functions, as long as all their uses are in such functions. A use from
// anything else (code of a function that stays, an initializer) means the
// function is needed; code generation's own calls (to __udivsi3, say) are not
// uses, so those stay in their sections.
void PI32V2TargetObjectFile::analyze(const Module &M) const {
  Analyzed = &M;
  OwnSection.clear();
  const Triple &TT = M.getTargetTriple();
  TargetLibraryInfoImpl TLII(TT);
  TargetLibraryInfo TLI(TLII);
  RTLIB::RuntimeLibcallsInfo Libcalls(TT);
  for (const Function &F : M) {
    if (F.isDeclaration())
      continue;
    LibFunc LF;
    if (F.hasLocalLinkage() ? !F.use_empty()
                            : (TLI.getLibFunc(F.getName(), LF) && TLI.has(LF)) ||
                                  Libcalls.getSupportedLibcallImpl(
                                      F.getName()) != RTLIB::Unsupported)
      OwnSection.insert(&F);
  }
  auto OnlyFromOwn = [&](const Value *V) {
    SmallVector<const User *, 8> Work(V->users());
    SmallPtrSet<const User *, 8> Seen;
    while (!Work.empty()) {
      const User *U = Work.pop_back_val();
      if (!Seen.insert(U).second)
        continue;
      if (auto *I = dyn_cast<Instruction>(U)) {
        if (!OwnSection.count(I->getFunction()))
          return false;
      } else if (isa<ConstantExpr>(U)) {
        Work.append(U->user_begin(), U->user_end());
      } else {
        return false;
      }
    }
    return true;
  };
  for (bool Changed = true; Changed;) {
    Changed = false;
    for (const Function &F : M)
      if (OwnSection.count(&F) && !OnlyFromOwn(&F)) {
        OwnSection.erase(&F);
        Changed = true;
      }
  }
}

MCSection *PI32V2TargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  if (Kind.isText() && !TM.getFunctionSections() && !GO->hasComdat()) {
    if (Analyzed != GO->getParent())
      analyze(*GO->getParent());
    if (!SharedText || OwnSection.count(GO))
      return getContext().getELFSection(
          ".text." + TM.getSymbol(GO)->getName(), ELF::SHT_PROGBITS,
          ELF::SHF_ALLOC | ELF::SHF_EXECINSTR);
  }
  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
}
