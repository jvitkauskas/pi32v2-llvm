//===-- PI32V2Subtarget.cpp - PI32V2 subtarget ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2Subtarget.h"
#include "MCTargetDesc/PI32V2MCTargetDesc.h"
#include "PI32V2SelectionDAGInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/SelectionDAGISel.h"

#define DEBUG_TYPE "pi32v2-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "PI32V2GenSubtargetInfo.inc"

using namespace llvm;

PI32V2Subtarget &
PI32V2Subtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS) {
  ParseSubtargetFeatures(CPU.empty() ? "generic" : CPU,
                         CPU.empty() ? "generic" : CPU, FS);
  return *this;
}

PI32V2Subtarget::PI32V2Subtarget(const Triple &TT, StringRef CPU, StringRef FS,
                                 const TargetMachine &TM)
    : PI32V2GenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this),
      FrameLowering(*this) {
  TSInfo = std::make_unique<PI32V2SelectionDAGInfo>();
}

PI32V2Subtarget::~PI32V2Subtarget() = default;

// The DAG in source order for speed: the ILP list scheduler (the default
// without a scheduling model) starts many values early, which a loop then
// keeps in registers or on the stack (Ripple's audio loops: 3 % more
// instructions). For size it measures a little smaller, so it stays there.
static ScheduleDAGSDNodes *createPI32V2DAGScheduler(SelectionDAGISel *IS,
                                                    CodeGenOptLevel OL) {
  if (OL != CodeGenOptLevel::None && IS->MF->getFunction().hasOptSize())
    return createILPListDAGScheduler(IS, OL);
  return createSourceListDAGScheduler(IS, OL);
}

RegisterScheduler::FunctionPassCtor
PI32V2Subtarget::getDAGScheduler(CodeGenOptLevel) const {
  return createPI32V2DAGScheduler;
}

// A region that ends at a call (the call, which moves sp, bounds it) sets up the
// arguments: scheduled top-down, a value stored to the stack is stored before
// r0..r3 are loaded; bottom-up kept it live across them, in a register that
// then had to be saved (a push and a pop in the function). Elsewhere both
// directions, which measured faster.
void PI32V2Subtarget::overrideSchedPolicy(MachineSchedPolicy &Policy,
                                          const SchedRegion &Region) const {
  MachineBasicBlock::iterator End = Region.RegionEnd;
  if (End != Region.RegionBegin->getParent()->end() && End->isCall())
    Policy.OnlyTopDown = true;
}
