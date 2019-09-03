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
#include "gizmo/ASTDiff.h"
#include "gizmo/ASTPatch.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Frontend/FrontendActions.h"
#include "llvm/Support/CommandLine.h"


using namespace llvm;
using namespace clang;
using namespace clang::tooling;

static cl::OptionCategory GizmoCategory("autograft-instrument options");
static cl::opt<std::string> SourcePath("source", cl::desc("<source>"), cl::Required, cl::cat(GizmoCategory));

std::list<std::string> variableNameList;
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


int traverseNode(clang::diff::NodeRef node){
  auto ChildBegin = node.begin(), ChildEnd = node.end();
//  llvm::outs() << node.getTypeLabel() << "\n";
  std::string nodeType = node.getTypeLabel();
  int nodeId = node.getId();
  auto startLoc = node.getSourceBeginLocation();
  int locLineNumber = startLoc.first;
  if(locLineNumber == stoi(LineNumber)){
    targetNodeId = nodeId;
    return 1;
  }



  if (nodeType == "ParmVarDecl" || nodeType == "VarDecl"){
    auto identifier = node.getIdentifier();
    variableNameList.push_front(*identifier);
  } else if (nodeType == "MemberExpr"){
    std::string varName;
    std::string nodeValue = node.getValue();
    if (nodeValue == "")
      return 0;

    std::string identifier =  nodeValue.substr(nodeValue.find("::") + 2);
    clang::diff::NodeRef childNode = *ChildBegin;
    std::string childNodeType = childNode.getTypeLabel();
    if (childNodeType != "DeclRefExpr")
      return 0;

    varName = childNode.getValue() + "->" + identifier;
//    llvm::outs() << varName << "\n";


    for (it=variableNameList.begin(); it!=variableNameList.end(); ++it)
      if (*it == varName)
        return 0;
    variableNameList.push_front(varName);
    return 0;
  }


  if (ChildBegin != ChildEnd) {
    traverseNode(*ChildBegin);
    for (++ChildBegin; ChildBegin != ChildEnd; ++ChildBegin) {
      int ret = traverseNode(*ChildBegin);
      if (ret == 1)
        return 1;
    }
  }

  return 0;

}



void insertCode(clang::diff::NodeRef targetNode, Rewriter &rewriter) {
  std::string insertStatement;


  clang::diff::NodeRef targetParentNode = *targetNode.getParent();
  int offset = targetNode.findPositionInParent();
  clang::diff::NodeRef nearestChildNode = targetParentNode.getChild(offset - 1);
  SourceLocation insertLoc = nearestChildNode.getSourceRange().getEnd();


  for (it=variableNameList.begin(); it!=variableNameList.end(); ++it){
//    llvm::outs() << *it << "\n";
    std::string varName = *it;
    std::string codeToInsert = "\n";
    codeToInsert = "klee_print_expr(\"[var-expr] " + varName + "\", " + varName + ");\n";
    insertStatement += codeToInsert;
  }


    if (rewriter.InsertTextAfterToken(insertLoc, insertStatement))
      llvm::errs() << "error inserting\n";



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

//   llvm::outs()  << "/* Start Crochet Output */\n";

//  clang::diff::NodeRef rootNode = SrcTree.getRoot();

  std::size_t found = SourcePath.find_last_of("/\\");
  std::string sourceFileName = SourcePath.substr(found+1);

  for (diff::NodeRef node : SrcTree) {
    auto StartLoc = node.getSourceBeginLocation();
    auto EndLoc = node.getSourceEndLocation();
    int startLine = StartLoc.first;
    int endLine = EndLoc.first;
    int lineNumber = stoi(LineNumber);
    std::string nodeType = node.getTypeLabel();
    if (startLine <= lineNumber && endLine >= lineNumber){
      if (nodeType == "FunctionDecl"){
        std::string fileName = node.getFileName();
        if (!fileName.empty()) {
          if (fileName == sourceFileName){
//            llvm::outs() << node.getValue() << "\n";
            traverseNode(node);
            break;
          }
        }

      }

    }
  }

//  llvm::outs() << targetNodeId << "\n";
  clang::diff::NodeRef targetNode = SrcTree.getNode(targetNodeId);
  insertCode(targetNode, rewriter);

  const RewriteBuffer *rewriteBuf = rewriter.getRewriteBufferFor(SrcTree.getSourceManager().getMainFileID());
  std::string includeHeader = "\n#include<klee/klee.h>\n";
  llvm::outs() << includeHeader;
  llvm::outs() << std::string(rewriteBuf->begin(), rewriteBuf->end());

  return 0;

}
