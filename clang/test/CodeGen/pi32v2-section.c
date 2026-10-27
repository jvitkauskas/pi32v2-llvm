// RUN: %clang_cc1 -triple pi32v2 -fms-extensions -emit-llvm -o - %s 2>/dev/null | FileCheck %s --check-prefixes=CHECK,PRAGMA
// RUN: %clang_cc1 -triple pi32v2 -fms-extensions -fprefer-gnu-section -emit-llvm -o - %s 2>/dev/null | FileCheck %s --check-prefixes=CHECK,GNU
// REQUIRES: pi32v2-registered-target

// -fprefer-gnu-section (JieLi's option): a section attribute written on a function definition replaces the section
// '#pragma code_seg' gave it, where it is otherwise ignored with a warning. Variables and the section attribute of
// an earlier declaration are not affected. (Both runs warn about proto_attr; only the first also about both.)

int proto_attr(int) __attribute__((section(".ap")));

#pragma code_seg(".pragma")
__attribute__((section(".attr"))) int both(int x) { return x + 1; }
int pragma_only(int x) { return x + 2; }
int proto_attr(int x) { return x + 3; }
__attribute__((section(".dattr"))) int dattr = 4;
#pragma code_seg()
__attribute__((section(".plain"))) int plain(int x) { return x + 5; }

// CHECK-DAG: @dattr = {{.*}}section ".dattr"
// PRAGMA-DAG: define {{.*}} @both({{.*}} section ".pragma"
// GNU-DAG: define {{.*}} @both({{.*}} section ".attr"
// CHECK-DAG: define {{.*}} @pragma_only({{.*}} section ".pragma"
// CHECK-DAG: define {{.*}} @proto_attr({{.*}} section ".pragma"
// CHECK-DAG: define {{.*}} @plain({{.*}} section ".plain"
