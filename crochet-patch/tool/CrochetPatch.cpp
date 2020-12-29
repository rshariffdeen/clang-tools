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

#include "crochet/ASTDiff.h"
#include "crochet/ASTPatch.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"


using namespace llvm;
using namespace clang;
using namespace clang::tooling;


static cl::OptionCategory CrochetPatchCategory("gizmo-instrument-patch options");


static cl::opt<std::string> ScriptPath("script", cl::desc("<script>"), cl::Required, cl::cat(CrochetPatchCategory));
static cl::opt<std::string> TargetPath("target", cl::desc("<target>"), cl::Required, cl::cat(CrochetPatchCategory));
static cl::opt<std::string> SourcePath("source", cl::desc("<source>"), cl::Required, cl::cat(CrochetPatchCategory));
static cl::opt<std::string> DestinationPath("destination", cl::desc("<destination>"), cl::Required, cl::cat(CrochetPatchCategory));
static cl::opt<std::string> MapPath("map", cl::desc("<variable mapping>"), cl::Required, cl::cat(CrochetPatchCategory));
static cl::opt<std::string> StopAfter("stop-diff-after", cl::desc("<topdown|bottomup>"), cl::Optional, cl::init(""), cl::cat(CrochetPatchCategory));

static cl::opt<int> MaxSize("s", cl::desc("<maxsize>"), cl::Optional, cl::init(-1), cl::cat(CrochetPatchCategory));
static cl::opt<float> MinSimilarity("min-sim", cl::desc("<minsimilarity>"), cl::Optional, cl::init(-1), cl::cat(CrochetPatchCategory));
static cl::opt<std::string> BuildPath("p", cl::desc("Build path"), cl::init(""), cl::Optional, cl::cat(CrochetPatchCategory));
static cl::list<std::string> ArgsAfter("extra-arg", cl::desc("Additional argument to append to the compiler command line"), cl::cat(CrochetPatchCategory));
static cl::list<std::string> ArgsBefore("extra-arg-before", cl::desc("Additional argument to prepend to the compiler command line"), cl::cat(CrochetPatchCategory));

static void addExtraArgs(std::unique_ptr<CompilationDatabase> &Compilations) {
    if (!Compilations)
        return;
    auto AdjustingCompilations =
            std::make_unique<ArgumentsAdjustingCompilations>(
                    std::move(Compilations));


    if (reference == "A"){
        AdjustingCompilations->appendArgumentsAdjuster(
                getInsertArgumentAdjuster(ArgsBeforeA, ArgumentInsertPosition::BEGIN));
        AdjustingCompilations->appendArgumentsAdjuster(
                getInsertArgumentAdjuster(ArgsAfterA, ArgumentInsertPosition::END));
    } else{
        AdjustingCompilations->appendArgumentsAdjuster(
                getInsertArgumentAdjuster(ArgsBeforeC, ArgumentInsertPosition::BEGIN));
        AdjustingCompilations->appendArgumentsAdjuster(
                getInsertArgumentAdjuster(ArgsAfterC, ArgumentInsertPosition::END));
    }

    Compilations = std::move(AdjustingCompilations);
}


static std::unique_ptr<CompilationDatabase>
getCompilationDatabase(StringRef Filename) {
  std::string ErrorMessage;
  std::unique_ptr<CompilationDatabase> Compilations =
      CompilationDatabase::autoDetectFromSource(
          BuildPath.empty() ? Filename : BuildPath, ErrorMessage);
  if (!Compilations) {
    llvm::errs()
        << "Error while trying to load a compilation database, running "
           "without flags.\n"
        << ErrorMessage;
    Compilations = std::make_unique<clang::tooling::FixedCompilationDatabase>(
        ".", std::vector<std::string>());
  }

    if (Filename == SourcePath || Filename == DestinationPath)
        addExtraArgs(Compilations, "A");
    else
        addExtraArgs(Compilations, "C");
  return Compilations;
}






bool in_array(const std::string &value, const std::vector<std::string> &array)
{
    return std::find(array.begin(), array.end(), value) != array.end();
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
    std::unique_ptr <CompilationDatabase> CommonCompilationsA =
            FixedCompilationDatabase::loadFromCommandLine(argc, argv, ErrorMessage);
    if (!CommonCompilationsA && !ErrorMessage.empty())
        llvm::errs() << ErrorMessage;
    std::unique_ptr <CompilationDatabase> CommonCompilationsC =
            FixedCompilationDatabase::loadFromCommandLine(argc, argv, ErrorMessage);
    if (!CommonCompilationsC && !ErrorMessage.empty())
        llvm::errs() << ErrorMessage;
    cl::HideUnrelatedOptions(ClangDiffCategory);
    if (!cl::ParseCommandLineOptions(argc, argv)) {
        cl::PrintOptionValues();
        return 1;
    }

    addExtraArgs(CommonCompilationsA, "A");
    addExtraArgs(CommonCompilationsC, "C");
 
  
  std::unique_ptr<ASTUnit> Src = getAST(CommonCompilations, SourcePath);
  // llvm::outs() << "Building AST for destination\n";
  std::unique_ptr<ASTUnit> Dst = getAST(CommonCompilations, DestinationPath);
 
  if (!Src || !Dst){
    if (!Src)
      llvm::errs() << "Error: Could not build AST for source\n";
    if (!Dst)
      llvm::errs() << "Error: Could not build AST for destination\n";

    return 1;
  }

  diff::ComparisonOptions Options;
  if (MaxSize != -1)
    Options.MaxSize = MaxSize;
  if (!StopAfter.empty()) {
    if (StopAfter == "topdown")
      Options.StopAfterTopDown = true;
    else if (StopAfter == "bottomup")
      Options.StopAfterBottomUp = true;
    else {
      llvm::errs() << "Error: Invalid argument for -stop-diff-after\n";
      return 1;
    }
  }

  // llvm::outs() << "Building AST for target\n";

 
  std::unique_ptr<CompilationDatabase> FileCompilations;
  if (!CommonCompilations)
    FileCompilations = getCompilationDatabase(TargetPath);

  std::array<std::string, 1> Files = {{TargetPath}};
  RefactoringTool TargetTool(CommonCompilations ? *CommonCompilations : *FileCompilations, Files);
  std::vector<std::unique_ptr<ASTUnit>> TargetASTs;
  TargetTool.buildASTs(TargetASTs);

  if (TargetASTs.size() == 0){
    llvm::errs() << "Error: Could not build AST for target\n";
    return 1;
  }
    

  std::unique_ptr<ASTUnit> Tgt = std::move(TargetASTs[0]);

  // llvm::outs() << "Creating synax trees\n";

  diff::SyntaxTree SrcTree(*Src);
  diff::SyntaxTree DstTree(*Dst);
  diff::SyntaxTree TgtTree(*Tgt);

  
  if (auto Err = diff::patch(TargetTool, MapPath, SrcTree, DstTree, ScriptPath, Options)) {
      llvm::handleAllErrors(
          std::move(Err),
          [](const diff::PatchingError &PE) { PE.log(llvm::errs()); },
          [](const ReplacementError &RE) { RE.log(llvm::errs()); });
      llvm::errs() << "*** errors occured, patching failed.\n";
      return 1;
    }

  return 0;
}
