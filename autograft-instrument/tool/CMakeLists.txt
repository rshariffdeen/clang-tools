set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_executable(AutoGraft
        AutoGraft.cpp
  )

target_link_libraries(AutoGraft
  PRIVATE
  AutoGraft
  clangBasic
  clangFrontend
  clangRewrite
  clangTooling 
  clangToolingCore
  )

install(TARGETS autograft
  RUNTIME DESTINATION bin)
