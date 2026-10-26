// RUN: %clang_cc1 -triple pi32v2 -O1 -emit-llvm -o - %s | FileCheck %s
// REQUIRES: pi32v2-registered-target
// 64-bit types are word-aligned in structures, variables and on the stack,
// as JieLi's compiler has them.

// CHECK-DAG: @gl = {{.*}}global i64 0, align 4
// CHECK-DAG: @gd = {{.*}}global double 0.000000e+00, align 4
long long gl;
double gd;

struct S { char c; long long x; };
// CHECK-DAG: @off = {{.*}}global i32 4, align 4
// CHECK-DAG: @size = {{.*}}global i32 12, align 4
int off = __builtin_offsetof(struct S, x), size = sizeof(struct S);

void g(long long *, double *);
// CHECK-LABEL: @locals(
// CHECK: alloca i64, align 4
// CHECK: alloca double, align 4
void locals(void) {
  long long r;
  double d;
  g(&r, &d);
}
