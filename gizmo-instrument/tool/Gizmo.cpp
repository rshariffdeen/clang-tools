//===- ClangDiff.cpp - compare source files by AST nodes ------*- C++ -*- -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a tool for syntax tree based comparison using
// Tooling/ASTDiff.
//
//===----------------------------------------------------------------------===//

#include "gizmo/ASTDiff.h"
#include "gizmo/ASTPatch.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"


using namespace llvm;
using namespace clang;
using namespace clang::tooling;

static cl::OptionCategory GizmoCategory("gizmo-instrument options");
static cl::opt<std::string> LineNumber("line-number", cl::desc("<line number in the source code>"), cl::Required, cl::cat(GizmoCategory));
static cl::opt<std::string> Transformation("transformation", cl::desc("<transformation type>"), cl::Required, cl::cat(GizmoCategory));
static cl::opt<std::string> SourcePath("source", cl::desc("<source>"), cl::Required, cl::cat(GizmoCategory));


static std::unique_ptr<CompilationDatabase>
getCompilationDatabase(StringRef Filename) {
  std::string ErrorMessage;
  std::unique_ptr<CompilationDatabase> Compilations =
      CompilationDatabase::autoDetectFromSource(Filename, ErrorMessage);
  if (!Compilations) {
    llvm::errs()
        << "Error while trying to load a compilation database, running "
           "without flags.\n"
        << ErrorMessage;
    Compilations = llvm::make_unique<clang::tooling::FixedCompilationDatabase>(
        ".", std::vector<std::string>());
  }
  return Compilations;
}



static std::unique_ptr<ASTUnit>
getAST(const std::unique_ptr<CompilationDatabase> &CommonCompilations,
       const StringRef Filename) {
  std::array<std::string, 1> Files = {{Filename}};
  std::unique_ptr<CompilationDatabase> FileCompilations;
  if (!CommonCompilations)
    FileCompilations = getCompilationDatabase(Filename);
  ClangTool Tool(CommonCompilations ? *CommonCompilations : *FileCompilations,
                 Files);
  std::vector<std::unique_ptr<ASTUnit>> ASTs;
  Tool.buildASTs(ASTs);
  if (ASTs.size() == 0){
    llvm::errs() << "Error: no AST built\n";
    return NULL;
  }
  if (ASTs.size() != Files.size()){    
    llvm::errs() << "more than one tree was built\n";
  }
  
  return std::move(ASTs[0]);
}



int main(int argc, const char **argv) {
 
  std::string ErrorMessage;
  bool modified = false;
  std::unique_ptr<CompilationDatabase> CommonCompilations =
      FixedCompilationDatabase::loadFromCommandLine(argc, argv, ErrorMessage);
  if (!CommonCompilations && !ErrorMessage.empty())
    llvm::errs() << ErrorMessage;
  if (!cl::ParseCommandLineOptions(argc, argv)) {
    cl::PrintOptionValues();
    return 1;
  }

  std::unique_ptr<ASTUnit> Src = getAST(CommonCompilations, SourcePath);

  if (!Src){
    llvm::errs() << "Error: Could not build AST for source\n";
    return 1;
  }

  clang::diff::SyntaxTree SrcTree(*Src);
  Rewriter Rewrite;
  SourceManager &SM = Src->getSourceManager();
  const LangOptions &LangOpts = Src->getLangOpts();
  Rewrite.setSourceMgr(SM, LangOpts);
  const RewriteBuffer *RewriteBuf = Rewrite.getRewriteBufferFor(SM.getMainFileID());
  // llvm::outs()  << "/* Start Crochet Output */\n";

  clang::diff::NodeRef rootNode = SrcTree.getRoot();
  llvm::outs() << rootNode.getTypeLabel() << "\n";

  for (diff::NodeRef Node : SrcTree) {
    auto StartLoc = Node.getSourceBeginLocation();
    auto EndLoc = Node.getSourceEndLocation();
    int startLine = StartLoc.first;
    int endLine = EndLoc.first;
    int lineNumber = stoi(LineNumber);
    if (startLine <= lineNumber && endLine >= lineNumber){
        llvm::outs() << Node.getTypeLabel() << "\n";
        break;
    }

  }

  if (modified)
    llvm::outs() << std::string(RewriteBuf->begin(), RewriteBuf->end());


  return 0;
}
