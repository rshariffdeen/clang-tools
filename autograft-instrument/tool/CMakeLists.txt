set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_executable(autograft
        AutoGraft.cpp
  )

target_link_libraries(autograft
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
