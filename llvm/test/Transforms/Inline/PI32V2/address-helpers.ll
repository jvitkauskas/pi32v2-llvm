; REQUIRES: pi32v2-registered-target
; RUN: opt -mtriple=pi32v2 -passes='require<profile-summary>,cgscc(inline)' -S %s | FileCheck %s
;
; Let small pure address calculations expose bounds and offsets to the caller.
; Bound duplication and preserve minsize, hints, memory effects and escapes.

target triple = "pi32v2"
@objects = external global [3 x [5 x [12 x i8]]]
@keep = global ptr @escaping

define internal ptr @address(i32 %part, i32 %slot, ptr %out) memory(none) willreturn {
  %a = icmp ult i32 %part, 3
  %b = icmp ult i32 %slot, 5
  %ok = and i1 %a, %b
  %shift = lshr i32 %slot, 2
  %index = xor i32 %shift, %slot
  %offset = and i32 %index, 3
  %p = getelementptr inbounds [3 x [5 x [12 x i8]]], ptr @objects, i32 0, i32 %part, i32 %offset
  %r = select i1 %ok, ptr %p, ptr null
  ret ptr %r
}

; CHECK-LABEL: define ptr @address_caller0(
; CHECK-NOT: call ptr @address(
; CHECK: ret ptr
define ptr @address_caller0(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @address(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @address_caller1(
; CHECK-NOT: call ptr @address(
; CHECK: ret ptr
define ptr @address_caller1(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @address(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @address_caller2(
; CHECK-NOT: call ptr @address(
; CHECK: ret ptr
define ptr @address_caller2(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @address(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

define internal ptr @shared(i32 %part, i32 %slot, ptr %out) memory(none) willreturn {
  %a = icmp ult i32 %part, 3
  %b = icmp ult i32 %slot, 5
  %ok = and i1 %a, %b
  %shift = lshr i32 %slot, 2
  %index = xor i32 %shift, %slot
  %offset = and i32 %index, 3
  %p = getelementptr inbounds [3 x [5 x [12 x i8]]], ptr @objects, i32 0, i32 %part, i32 %offset
  %r = select i1 %ok, ptr %p, ptr null
  ret ptr %r
}

; CHECK-LABEL: define ptr @shared_caller0(
; CHECK: call ptr @shared(
; CHECK: ret ptr
define ptr @shared_caller0(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @shared(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @shared_caller1(
; CHECK: call ptr @shared(
; CHECK: ret ptr
define ptr @shared_caller1(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @shared(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @shared_caller2(
; CHECK: call ptr @shared(
; CHECK: ret ptr
define ptr @shared_caller2(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @shared(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @shared_caller3(
; CHECK: call ptr @shared(
; CHECK: ret ptr
define ptr @shared_caller3(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @shared(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

define internal ptr @memory_address(i32 %part, i32 %slot, ptr %out) memory(argmem: write) willreturn {
  %a = icmp ult i32 %part, 3
  %b = icmp ult i32 %slot, 5
  %ok = and i1 %a, %b
  %shift = lshr i32 %slot, 2
  %index = xor i32 %shift, %slot
  %offset = and i32 %index, 3
  %p = getelementptr inbounds [3 x [5 x [12 x i8]]], ptr @objects, i32 0, i32 %part, i32 %offset
  %r = select i1 %ok, ptr %p, ptr null
  store ptr %r, ptr %out
  ret ptr %r
}

; CHECK-LABEL: define ptr @memory_address_caller0(
; CHECK: call ptr @memory_address(
; CHECK: ret ptr
define ptr @memory_address_caller0(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @memory_address(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @memory_address_caller1(
; CHECK: call ptr @memory_address(
; CHECK: ret ptr
define ptr @memory_address_caller1(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @memory_address(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @memory_address_caller2(
; CHECK: call ptr @memory_address(
; CHECK: ret ptr
define ptr @memory_address_caller2(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @memory_address(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

define internal ptr @no_termination_proof(i32 %part, i32 %slot, ptr %out) memory(none) {
  %a = icmp ult i32 %part, 3
  %b = icmp ult i32 %slot, 5
  %ok = and i1 %a, %b
  %shift = lshr i32 %slot, 2
  %index = xor i32 %shift, %slot
  %offset = and i32 %index, 3
  %p = getelementptr inbounds [3 x [5 x [12 x i8]]], ptr @objects, i32 0, i32 %part, i32 %offset
  %r = select i1 %ok, ptr %p, ptr null
  ret ptr %r
}

; CHECK-LABEL: define ptr @no_termination_proof_caller0(
; CHECK: call ptr @no_termination_proof(
; CHECK: ret ptr
define ptr @no_termination_proof_caller0(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @no_termination_proof(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @no_termination_proof_caller1(
; CHECK: call ptr @no_termination_proof(
; CHECK: ret ptr
define ptr @no_termination_proof_caller1(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @no_termination_proof(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @no_termination_proof_caller2(
; CHECK: call ptr @no_termination_proof(
; CHECK: ret ptr
define ptr @no_termination_proof_caller2(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @no_termination_proof(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

define internal ptr @escaping(i32 %part, i32 %slot, ptr %out) memory(none) willreturn {
  %a = icmp ult i32 %part, 3
  %b = icmp ult i32 %slot, 5
  %ok = and i1 %a, %b
  %shift = lshr i32 %slot, 2
  %index = xor i32 %shift, %slot
  %offset = and i32 %index, 3
  %p = getelementptr inbounds [3 x [5 x [12 x i8]]], ptr @objects, i32 0, i32 %part, i32 %offset
  %r = select i1 %ok, ptr %p, ptr null
  ret ptr %r
}

; CHECK-LABEL: define ptr @escaping_caller0(
; CHECK: call ptr @escaping(
; CHECK: ret ptr
define ptr @escaping_caller0(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @escaping(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

define internal ptr @hinted(i32 %part, i32 %slot, ptr %out) memory(none) willreturn inlinehint "function-inline-cost"="425" {
  %a = icmp ult i32 %part, 3
  %b = icmp ult i32 %slot, 5
  %ok = and i1 %a, %b
  %shift = lshr i32 %slot, 2
  %index = xor i32 %shift, %slot
  %offset = and i32 %index, 3
  %p = getelementptr inbounds [3 x [5 x [12 x i8]]], ptr @objects, i32 0, i32 %part, i32 %offset
  %r = select i1 %ok, ptr %p, ptr null
  ret ptr %r
}

; CHECK-LABEL: define ptr @hinted_caller0(
; CHECK: call ptr @hinted(
; CHECK: ret ptr
define ptr @hinted_caller0(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @hinted(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @hinted_caller1(
; CHECK: call ptr @hinted(
; CHECK: ret ptr
define ptr @hinted_caller1(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @hinted(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @hinted_caller2(
; CHECK: call ptr @hinted(
; CHECK: ret ptr
define ptr @hinted_caller2(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @hinted(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

define internal i32 @scalar(i32 %part, i32 %slot, ptr %out) memory(none) willreturn {
  %a = icmp ult i32 %part, 3
  %b = icmp ult i32 %slot, 5
  %ok = and i1 %a, %b
  %shift = lshr i32 %slot, 2
  %index = xor i32 %shift, %slot
  %offset = and i32 %index, 3
  %p = getelementptr inbounds [3 x [5 x [12 x i8]]], ptr @objects, i32 0, i32 %part, i32 %offset
  %r = select i1 %ok, ptr %p, ptr null
  %integer = ptrtoint ptr %r to i32
  ret i32 %integer
}

; CHECK-LABEL: define i32 @scalar_caller0(
; CHECK: call i32 @scalar(
; CHECK: ret i32
define i32 @scalar_caller0(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call i32 @scalar(i32 %part, i32 %slot, ptr %out)
  ret i32 %r
}

; CHECK-LABEL: define i32 @scalar_caller1(
; CHECK: call i32 @scalar(
; CHECK: ret i32
define i32 @scalar_caller1(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call i32 @scalar(i32 %part, i32 %slot, ptr %out)
  ret i32 %r
}

; CHECK-LABEL: define i32 @scalar_caller2(
; CHECK: call i32 @scalar(
; CHECK: ret i32
define i32 @scalar_caller2(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call i32 @scalar(i32 %part, i32 %slot, ptr %out)
  ret i32 %r
}

define internal ptr @minimum(i32 %part, i32 %slot, ptr %out) memory(none) willreturn {
  %a = icmp ult i32 %part, 3
  %b = icmp ult i32 %slot, 5
  %ok = and i1 %a, %b
  %shift = lshr i32 %slot, 2
  %index = xor i32 %shift, %slot
  %offset = and i32 %index, 3
  %p = getelementptr inbounds [3 x [5 x [12 x i8]]], ptr @objects, i32 0, i32 %part, i32 %offset
  %r = select i1 %ok, ptr %p, ptr null
  ret ptr %r
}

; CHECK-LABEL: define ptr @minsize_caller(
; CHECK: call ptr @minimum(
define ptr @minsize_caller(i32 %part, i32 %slot, ptr %out) minsize optsize {
  %r = call ptr @minimum(i32 %part, i32 %slot, ptr %out)
  ret ptr %r
}

; CHECK-LABEL: define ptr @noinline_caller(
; CHECK: call ptr @minimum(
define ptr @noinline_caller(i32 %part, i32 %slot, ptr %out) optsize {
  %r = call ptr @minimum(i32 %part, i32 %slot, ptr %out) noinline
  ret ptr %r
}
