add_llvm_component_library(LLVMPI32V2AsmParser
  PI32V2AsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  Support
  PI32V2Desc
  PI32V2Info

  ADD_TO_COMPONENT
  PI32V2
  )
