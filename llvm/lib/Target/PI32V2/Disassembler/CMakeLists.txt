add_llvm_component_library(LLVMPI32V2Disassembler
  PI32V2Disassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  MC
  Support
  PI32V2Desc
  PI32V2Info

  ADD_TO_COMPONENT
  PI32V2
  )
