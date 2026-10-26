; RUN: llvm-as < %s | llvm-dis | FileCheck %s

; JieLi's clang (LLVM 4) gives pi32v2 no stack alignment; it is a word.

target datalayout = "e-m:e-p:32:32-i1:32-i64:32:32-f64:32:32-n8:16:32-a:0:32"
target triple = "pi32v2"

; CHECK: target datalayout = "e-m:e-p:32:32-i1:32-i64:32:32-f64:32:32-n8:16:32-a:0:32-S32"
