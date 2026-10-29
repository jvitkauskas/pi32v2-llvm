//===-- PI32V2MachineFunctionInfo.h - PI32V2 function info ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_PI32V2MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_PI32V2_PI32V2MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"
#include <optional>

namespace llvm {

/// A variadic function pushes the argument registers its named arguments left
/// unused just below the incoming sp, so they and the stack arguments are one
/// run of words (pi32v2-isa docs/ABI.md section 5.5).
class PI32V2MachineFunctionInfo : public MachineFunctionInfo {
  int VarArgsFrameIndex = 0;
  unsigned FirstVarArgReg = 4; // r0..r3: the first unused one; 4: none
  int VarArgsSaveIndex = 0;
  // Allocation hints can change eviction/splitting in unrelated live ranges.
  // Cache the CFG-wide safety decision for the duration of allocation.
  mutable std::optional<bool> HasAllocationLoop;

public:
  PI32V2MachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override {
    return DestMF.cloneInfo<PI32V2MachineFunctionInfo>(*this);
  }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }
  unsigned getFirstVarArgReg() const { return FirstVarArgReg; }
  void setFirstVarArgReg(unsigned R) { FirstVarArgReg = R; }
  /// Bytes the prologue pushes for the unused argument registers.
  unsigned getVarArgsSaveSize() const { return 4 * (4 - FirstVarArgReg); }
  int getVarArgsSaveIndex() const { return VarArgsSaveIndex; }
  void setVarArgsSaveIndex(int FI) { VarArgsSaveIndex = FI; }
  bool hasAllocationLoop(const MachineFunction &MF) const;
};
} // namespace llvm

#endif
