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
#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <list>
#include <vector>
#include "autograft/ASTDiff.h"
#include "autograft/ASTPatch.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Frontend/FrontendActions.h"
#include "llvm/Support/CommandLine.h"


using namespace llvm;
using namespace clang;
using namespace clang::tooling;

static cl::OptionCategory GizmoCategory("gizmo-instrument options");
static cl::opt<std::string> Transformation("transformation", cl::desc("<transformation type>"), cl::Optional, cl::cat(GizmoCategory));
static cl::opt<std::string> SourcePath("source", cl::desc("<source>"), cl::Required, cl::cat(GizmoCategory));


std::list<std::string>::iterator it;
int targetNodeId = 0;

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


int instrumentCode(clang::diff::NodeRef node, Rewriter &rewriter){
  auto ChildBegin = node.begin(), ChildEnd = node.end();
  std::string nodeType = node.getTypeLabel();
//  llvm::outs() << nodeType << "\n";
  int nodeId = node.getId();
  auto startLoc = node.getSourceBeginLocation();


  if (nodeType == "IfStmt"){
    auto ifNode = node.ASTNode.get<IfStmt>();
    auto condNode = ifNode->getCond();
    auto thenNode = ifNode->getThen();
    std::string instrumentFirst = "flip_callback( ";
    std::string instrumentSecond = ")";
//    clang::diff::NodeRef condNode = node.getChild(0);
//    int numChildren = condNode.getNumChildren();
//    llvm::outs() << condNode.getTypeLabel() << "\n";

    SourceLocation insertLocStart = condNode->getLocStart();
    SourceLocation insertLocEnd = thenNode->getLocStart();
//    llvm::outs() << insertLoc.printToString(rewriter.getSourceMgr()) << "\n";
    if (rewriter.InsertTextBefore(insertLocEnd, instrumentSecond))
      llvm::errs() << "error inserting second\n";
    if (rewriter.InsertTextBefore(insertLocStart, instrumentFirst))
      llvm::errs() << "error inserting first\n";


//
//    std::string condition;
//
//
//    clang::diff::NodeRef targetParentNode = *targetNode.getParent();
//    int offset = targetNode.findPositionInParent();
//    clang::diff::NodeRef nearestChildNode = targetParentNode.getChild(offset - 1);
//    SourceLocation insertLoc = nearestChildNode.getSourceRange().getEnd();
//
//
//    for (it=variableNameList.begin(); it!=variableNameList.end(); ++it){
//      std::string varName = *it;
//      std::string codeToInsert = "\n";
//      codeToInsert = "klee_print_expr(\"[var-expr] " + varName + "\", " + varName + ");\n";
//      insertStatement += codeToInsert;
//    }
//
//
//    if (rewriter.InsertTextAfterToken(insertLoc, insertStatement))
//      llvm::errs() << "error inserting\n";

  }

  if (ChildBegin != ChildEnd) {
    instrumentCode(*ChildBegin, rewriter);
    for (++ChildBegin; ChildBegin != ChildEnd; ++ChildBegin) {
      int ret = instrumentCode(*ChildBegin, rewriter);
      if (ret == 1)
        return 1;
    }
  }

  return 0;

}



int main(int argc, const char **argv) {

  std::string ErrorMessage;
  std::unique_ptr<CompilationDatabase> CommonCompilations =
          FixedCompilationDatabase::loadFromCommandLine(argc, argv, ErrorMessage);
  if (!CommonCompilations && !ErrorMessage.empty())
    llvm::errs() << ErrorMessage;
  if (!cl::ParseCommandLineOptions(argc, argv)) {
    cl::PrintOptionValues();
    return 1;
  }

  std::unique_ptr<CompilationDatabase> FileCompilations;
  if (!CommonCompilations)
    FileCompilations = getCompilationDatabase(SourcePath);

  std::array<std::string, 1> Files = {{SourcePath}};
  RefactoringTool RefactorTool(CommonCompilations ? *CommonCompilations : *FileCompilations, Files);
  std::vector<std::unique_ptr<ASTUnit>> SrcASTs;
  RefactorTool.buildASTs(SrcASTs);

  if (SrcASTs.size() == 0){
    llvm::errs() << "Error: Could not build AST for target\n";
    return 1;
  }

  clang::diff::SyntaxTree SrcTree(*SrcASTs[0]);
//  clang::diff::NodeRef rootNode = SrcTree.getRoot();
  Rewriter rewriter;
  SourceManager &SM = SrcTree.getSourceManager();
  const LangOptions &LangOpts = SrcTree.getLangOpts();
  rewriter.setSourceMgr(SM, LangOpts);

  std::size_t found = SourcePath.find_last_of("/\\");
  std::string sourceFileName = SourcePath.substr(found+1);

//  llvm::outs() << sourceFileName << "\n";

  for (diff::NodeRef node : SrcTree) {
    std::string nodeType = node.getTypeLabel();
      if (nodeType == "FunctionDecl"){
//        llvm::outs() << node.getValue() << "\n";
        std::string fileName = node.getFileName();
//        llvm::outs() << fileName << "\n";
        if (!fileName.empty()) {
          if (fileName == sourceFileName){
//            llvm::outs() << node.getValue() << "\n";
            instrumentCode(node, rewriter);
          }
        }



    }
  }


  const RewriteBuffer *rewriteBuf = rewriter.getRewriteBufferFor(SrcTree.getSourceManager().getMainFileID());
  llvm::outs() << std::string(rewriteBuf->begin(), rewriteBuf->end());

  return 0;

}
