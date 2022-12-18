set(LLVM_LINK_COMPONENTS support)

add_clang_library(clangTidyCaosModule
  SHARED

  IdentifierNamingCheck.cpp
  MagicNumbersCheck.cpp
  CaosTidyModule.cpp

  LINK_LIBS
  clangAST
  clangASTMatchers
  clangBasic
  clangLex
  clangTidy
  clangTidyUtils
  clangTooling
  )
