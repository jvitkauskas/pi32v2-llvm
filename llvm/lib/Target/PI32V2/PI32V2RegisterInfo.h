//===-- PI32V2RegisterInfo.h - PI32V2 registers -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_PI32V2REGISTERINFO_H
#define LLVM_LIB_TARGET_PI32V2_PI32V2REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "PI32V2GenRegisterInfo.inc"

namespace llvm {

class PI32V2RegisterInfo : public PI32V2GenRegisterInfo {
public:
  PI32V2RegisterInfo();

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }
  bool trackLivenessAfterRegAlloc(const MachineFunction &) const override {
    return true;
  }
  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;
  Register getFrameRegister(const MachineFunction &MF) const override;
};
} // namespace llvm

#endif
