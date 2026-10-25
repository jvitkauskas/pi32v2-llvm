//===-- PI32V2Subtarget.h - PI32V2 subtarget --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_PI32V2SUBTARGET_H
#define LLVM_LIB_TARGET_PI32V2_PI32V2SUBTARGET_H

#include "PI32V2FrameLowering.h"
#include "PI32V2ISelLowering.h"
#include "PI32V2InstrInfo.h"
#include "PI32V2RegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetMachine.h"

#define GET_SUBTARGETINFO_HEADER
#include "PI32V2GenSubtargetInfo.inc"

namespace llvm {

class PI32V2Subtarget : public PI32V2GenSubtargetInfo {
#define GET_SUBTARGETINFO_MACRO(ATTRIBUTE, DEFAULT, GETTER)                    \
  bool ATTRIBUTE = DEFAULT;
#include "PI32V2GenSubtargetInfo.inc"

  PI32V2InstrInfo InstrInfo;
  PI32V2TargetLowering TLInfo;
  std::unique_ptr<const SelectionDAGTargetInfo> TSInfo;
  PI32V2FrameLowering FrameLowering;

  PI32V2Subtarget &initializeSubtargetDependencies(StringRef CPU,
                                                   StringRef FS);

public:
  PI32V2Subtarget(const Triple &TT, StringRef CPU, StringRef FS,
                  const TargetMachine &TM);
  ~PI32V2Subtarget() override;

  const TargetFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const PI32V2InstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const PI32V2RegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }
  const PI32V2TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return TSInfo.get();
  }

  bool hasInstRev3() const { return HasInstRev3; }
  bool hasFPRev1() const { return HasFPRev1; }
  bool isAC791N() const { return IsAC791N; }

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);
};
} // namespace llvm

#endif
