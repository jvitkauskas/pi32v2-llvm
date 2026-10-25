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
