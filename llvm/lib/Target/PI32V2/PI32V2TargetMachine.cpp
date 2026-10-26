//===-- PI32V2TargetMachine.cpp - PI32V2 target machine -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2TargetMachine.h"
#include "PI32V2TargetTransformInfo.h"
#include "PI32V2.h"
#include "PI32V2MachineFunctionInfo.h"
#include "PI32V2TargetObjectFile.h"
#include "TargetInfo/PI32V2TargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializePI32V2Target() {
  RegisterTargetMachine<PI32V2TargetMachine> X(getThePI32V2Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializePI32V2AsmPrinterPass(PR);
  initializePI32V2DAGToDAGISelLegacyPass(PR);
  // Loop rotation with header duplication also at minsize (-Oz): it saves
  // bytes here (m-bootloader 1.2: 72 on the SMK-37, 52 on the FM-1), in clang
  // and in the linker's LTO alike. LoopRotation's own option, left alone when
  // given; it is process-wide (the toolchain is built for this target only).
  auto &Opts = cl::getRegisteredOptions();
  auto It = Opts.find("enable-loop-header-duplication-at-minsize");
  if (It != Opts.end() && !It->second->getNumOccurrences())
    if (auto *O = static_cast<cl::opt<bool> *>(It->second))
      *O = true;
}

PI32V2TargetMachine::PI32V2TargetMachine(const Target &T, const Triple &TT,
                                         StringRef CPU, StringRef FS,
                                         const TargetOptions &Options,
                                         std::optional<Reloc::Model> RM,
                                         std::optional<CodeModel::Model> CM,
                                         CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
                               RM.value_or(Reloc::Static),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<PI32V2TargetObjectFile>()) {
  initAsmInfo();
}

PI32V2TargetMachine::~PI32V2TargetMachine() = default;

const PI32V2Subtarget *
PI32V2TargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");
  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;
  auto &I = SubtargetMap[CPU + FS];
  if (!I)
    I = std::make_unique<PI32V2Subtarget>(TargetTriple, CPU, FS, *this);
  return I.get();
}

TargetTransformInfo
PI32V2TargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<PI32V2TTIImpl>(this, F));
}

MachineFunctionInfo *PI32V2TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return PI32V2MachineFunctionInfo::create<PI32V2MachineFunctionInfo>(Allocator,
                                                                      F, STI);
}

namespace {
class PI32V2PassConfig : public TargetPassConfig {
public:
  PI32V2PassConfig(PI32V2TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  PI32V2TargetMachine &getPI32V2TargetMachine() const {
    return getTM<PI32V2TargetMachine>();
  }

  // Static globals in one block, reached from one address by offset
  // (JieLi's compiler makes .L_MergedGlobals too); 255: the reach of a byte
  // access's offset.
  bool addPreISel() override {
    addPass(createPI32V2AlignAllocasPass());
    addPass(createPI32V2Div64PreparePass());
    if (getOptLevel() != CodeGenOptLevel::None)
      addPass(createGlobalMergePass(TM, 255, /*OnlyOptimizeForSize=*/false,
                                    /*MergeExternalByDefault=*/false));
    return false;
  }

  void addIRPasses() override {
    addPass(createAtomicExpandLegacyPass());
    TargetPassConfig::addIRPasses();
  }

  bool addInstSelector() override {
    addPass(createPI32V2ISelDag(getPI32V2TargetMachine(), getOptLevel()));
    return false;
  }

  // if (...) { } blocks for short conditional code, on the final layout.
  void addPreEmitPass2() override {
    if (getOptLevel() != CodeGenOptLevel::None)
      addPass(createPI32V2IfBlocksPass());
  }
};
} // namespace

TargetPassConfig *PI32V2TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new PI32V2PassConfig(*this, PM);
}
