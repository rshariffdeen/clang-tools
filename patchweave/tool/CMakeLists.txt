set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_executable(patchweave
        PatchWeave.cpp
  )

target_link_libraries(patchweave
  PRIVATE
  patchWeave
  clangBasic
  clangFrontend
  clangRewrite
  clangTooling 
  clangToolingCore
  )

install(TARGETS patchweave
  RUNTIME DESTINATION bin)
