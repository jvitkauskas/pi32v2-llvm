//===-- PI32V2SelectionDAGInfo.cpp - PI32V2 DAG nodes ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PI32V2SelectionDAGInfo.h"

#define GET_SDNODE_DESC
#include "PI32V2GenSDNodeInfo.inc"

using namespace llvm;

PI32V2SelectionDAGInfo::PI32V2SelectionDAGInfo()
    : SelectionDAGGenTargetInfo(PI32V2GenSDNodeInfo) {}

PI32V2SelectionDAGInfo::~PI32V2SelectionDAGInfo() = default;
