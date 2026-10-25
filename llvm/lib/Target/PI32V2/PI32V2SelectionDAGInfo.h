//===-- PI32V2SelectionDAGInfo.h - PI32V2 DAG nodes -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_PI32V2SELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_PI32V2_PI32V2SELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "PI32V2GenSDNodeInfo.inc"

namespace llvm {
class PI32V2SelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  PI32V2SelectionDAGInfo();
  ~PI32V2SelectionDAGInfo() override;
};
} // namespace llvm

#endif
