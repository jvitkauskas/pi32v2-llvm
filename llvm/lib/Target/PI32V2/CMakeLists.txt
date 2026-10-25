add_llvm_component_group(PI32V2)

set(LLVM_TARGET_DEFINITIONS PI32V2.td)

tablegen(LLVM PI32V2GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM PI32V2GenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM PI32V2GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM PI32V2GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM PI32V2GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM PI32V2GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(PI32V2CommonTableGen)

# Only the machine-code layer so far: the code generator registers nothing.
add_llvm_target(PI32V2CodeGen
  PI32V2TargetMachine.cpp

  LINK_COMPONENTS
  MC
  Support
  Target
  PI32V2Desc
  PI32V2Info

  ADD_TO_COMPONENT
  PI32V2
  )


add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
