//===-- PI32V2MCAsmInfo.h - PI32V2 assembly properties ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_MCTARGETDESC_PI32V2MCASMINFO_H
#define LLVM_LIB_TARGET_PI32V2_MCTARGETDESC_PI32V2MCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class PI32V2MCAsmInfo : public MCAsmInfoELF {
public:
  explicit PI32V2MCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
};
} // namespace llvm

#endif
