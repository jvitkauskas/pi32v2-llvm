// RUN: %clang_cc1 -triple pi32v2 -O1 -emit-llvm -o - %s | FileCheck %s
// REQUIRES: pi32v2-registered-target
// JieLi's __builtin_pi32v2_*: bit operations as generic IR, the rest as the
// instruction.

// CHECK-LABEL: @clz(
// CHECK: call {{.*}}i32 @llvm.ctlz.i32(i32 %a, i1 false)
int clz(int a) { return __builtin_pi32v2_clz(a); }

// CHECK-LABEL: @rev8(
// CHECK: call i32 @llvm.bswap.i32(i32 %a)
int rev8(int a) { return __builtin_pi32v2_rev8(a); }

// CHECK-LABEL: @field(
// CHECK: lshr i32 %a, 4
// CHECK: and i32 {{.*}}, 255
int field(int a) { return __builtin_pi32v2_uextra(a, 4, 8); }

// CHECK-LABEL: @irq_off(
// CHECK: call void asm sideeffect "cli", "~{memory}"()
void irq_off(void) { __builtin_pi32v2_cli(); }

// CHECK-LABEL: @flush(
// CHECK: call void asm sideeffect "flush [$0]", "r,~{memory}"(i32 %a)
void flush(int a) { __builtin_pi32v2_flush(a); }
