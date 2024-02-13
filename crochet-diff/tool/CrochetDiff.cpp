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
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace clang;
using namespace clang::tooling;

static cl::OptionCategory ClangDiffCategory("clang-diff options");

static cl::opt<bool>
        ASTDump("ast-dump",
                cl::desc("Print the internal representation of the AST."),
                cl::init(false), cl::cat(ClangDiffCategory));

static cl::opt<bool> ASTDumpJson(
        "ast-dump-json",
        cl::desc("Print the internal representation of the AST as JSON."),
        cl::init(false), cl::cat(ClangDiffCategory));

static cl::opt<bool> PrintMatches("dump-matches",
                                  cl::desc("Print the matched nodes."),
                                  cl::init(false), cl::cat(ClangDiffCategory));

static cl::opt<bool> HtmlDiff("html",
                              cl::desc("Output a side-by-side diff in HTML."),
                              cl::init(false), cl::cat(ClangDiffCategory));


static cl::opt <std::string> SourcePath(cl::Positional, cl::desc("<source>"),
                                        cl::Required,
                                        cl::cat(ClangDiffCategory));

static cl::opt <std::string> DestinationPath(cl::Positional,
                                             cl::desc("<destination>"),
                                             cl::Optional,
                                             cl::cat(ClangDiffCategory));

static cl::opt<int> ASTIndex("i", cl::desc("<AST index>"), cl::Optional,
                             cl::init(-1), cl::cat(ClangDiffCategory));


static cl::opt <std::string> StopAfter("stop-diff-after",
                                       cl::desc("<topdown|bottomup>"),
                                       cl::Optional, cl::init(""),
                                       cl::cat(ClangDiffCategory));

static cl::opt<int> MaxSize("s", cl::desc("<maxsize>"), cl::Optional,
                            cl::init(-1), cl::cat(ClangDiffCategory));

static cl::opt <std::string> BuildPath("p", cl::desc("Build path"), cl::init(""),
                                       cl::Optional, cl::cat(ClangDiffCategory));

static cl::list <std::string> ArgsAfter(
        "extra-arg",
        cl::desc("Additional argument to append to the compiler command"),
        cl::cat(ClangDiffCategory));

static cl::list <std::string> ArgsBefore(
        "extra-arg-before",
        cl::desc("Additional argument to prepend to the compiler command"),
        cl::cat(ClangDiffCategory));

static cl::list <std::string> ArgsAfterA(
        "extra-arg-a",
        cl::desc("Additional argument to append to the compiler command line for Pa"),
        cl::cat(ClangDiffCategory));

static cl::list <std::string> ArgsBeforeA(
        "extra-arg-before-a",
        cl::desc("Additional argument to prepend to the compiler command line for Pa"),
        cl::cat(ClangDiffCategory));

static cl::list <std::string> ArgsAfterC(
        "extra-arg-c",
        cl::desc("Additional argument to append to the compiler command line for Pc"),
        cl::cat(ClangDiffCategory));

static cl::list <std::string> ArgsBeforeC(
        "extra-arg-before-c",
        cl::desc("Additional argument to prepend to the compiler command line for Pc"),
        cl::cat(ClangDiffCategory));


static void addExtraArgs(std::unique_ptr <CompilationDatabase> &Compilations,
                         std::string reference) {

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
    }else if (reference == "C"){
        AdjustingCompilations->appendArgumentsAdjuster(
                getInsertArgumentAdjuster(ArgsBeforeC, ArgumentInsertPosition::BEGIN));
        AdjustingCompilations->appendArgumentsAdjuster(
                getInsertArgumentAdjuster(ArgsAfterC, ArgumentInsertPosition::END));
    }  else{
        AdjustingCompilations->appendArgumentsAdjuster(
                getInsertArgumentAdjuster(ArgsBefore, ArgumentInsertPosition::BEGIN));
        AdjustingCompilations->appendArgumentsAdjuster(
                getInsertArgumentAdjuster(ArgsAfter, ArgumentInsertPosition::END));
    }

    Compilations = std::move(AdjustingCompilations);
}

static std::unique_ptr <CompilationDatabase>
getCompilationDatabase(StringRef Filename) {
    std::string ErrorMessage;
    std::unique_ptr <CompilationDatabase> Compilations =
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

    if (ASTDumpJson){
        addExtraArgs(Compilations, "NONE");
    } else {
        if (Filename == SourcePath)
            addExtraArgs(Compilations, "A");
        else
            addExtraArgs(Compilations, "C");
    }

    return Compilations;
}

static std::unique_ptr <ASTUnit>
getAST(const std::unique_ptr <CompilationDatabase> &CommonCompilations,
       const StringRef Filename) {
    std::array<std::string, 1> Files = {{Filename.str()}};
    std::unique_ptr <CompilationDatabase> FileCompilations;
    if (!CommonCompilations)
        FileCompilations = getCompilationDatabase(Filename);
    ClangTool Tool(CommonCompilations ? *CommonCompilations : *FileCompilations,
                   Files);
    std::vector <std::unique_ptr<ASTUnit>> ASTs;
    Tool.buildASTs(ASTs);
    if (ASTs.size() == 0)
        return nullptr;
    return std::move(ASTs[0]);
}

static char hexdigit(int N) { return N &= 0xf, N + (N < 10 ? '0' : 'a' - 10); }

static const char HtmlDiffHeader[] = R"(
<html>
<head>
<meta charset='utf-8'/>
<style>
span.d { color: red; }
span.u { color: #cc00cc; }
span.i { color: green; }
span.m { font-weight: bold; }
span   { font-weight: normal; color: black; }
div.code {
  width: 48%;
  height: 98%;
  overflow: scroll;
  float: left;
  padding: 0 0 0.5% 0.5%;
  border: solid 2px LightGrey;
  border-radius: 5px;
}
</style>
</head>
<script type='text/javascript'>
highlightStack = []
function clearHighlight() {
  while (highlightStack.length) {
    var [l, r] = highlightStack.pop()
    document.getElementById(l).style.backgroundColor = 'inherit'
    if (r[1] != '-')
      document.getElementById(r).style.backgroundColor = 'inherit'
  }
}
function highlight(event) {
  var id = event.target['id']
  doHighlight(id)
}
function doHighlight(id) {
  clearHighlight()
  source = document.getElementById(id)
  if (!source.attributes['tid'])
    return
  var mapped = source
  while (mapped && mapped.parentElement && mapped.attributes['tid'].value.substr(1) === '-1')
    mapped = mapped.parentElement
  var tid = null, target = null
  if (mapped) {
    tid = mapped.attributes['tid'].value
    target = document.getElementById(tid)
  }
  if (source.parentElement && source.parentElement.classList.contains('code'))
    return
  source.style.backgroundColor = 'lightgrey'
  source.scrollIntoView()
  if (target) {
    if (mapped === source)
      target.style.backgroundColor = 'lightgrey'
    target.scrollIntoView()
  }
  highlightStack.push([id, tid])
  location.hash = '#' + id
}
function scrollToBoth() {
  doHighlight(location.hash.substr(1))
}
function changed(elem) {
  return elem.classList.length == 0
}
function nextChangedNode(prefix, increment, number) {
  do {
    number += increment
    var elem = document.getElementById(prefix + number)
  } while(elem && !changed(elem))
  return elem ? number : null
}
function handleKey(e) {
  var down = e.code === "KeyJ"
  var up = e.code === "KeyK"
  if (!down && !up)
    return
  var id = highlightStack[0] ? highlightStack[0][0] : 'R0'
  var oldelem = document.getElementById(id)
  var number = parseInt(id.substr(1))
  var increment = down ? 1 : -1
  var lastnumber = number
  var prefix = id[0]
  do {
    number = nextChangedNode(prefix, increment, number)
    var elem = document.getElementById(prefix + number)
    if (up && elem) {
      while (elem.parentElement && changed(elem.parentElement))
        elem = elem.parentElement
      number = elem.id.substr(1)
    }
  } while ((down && id !== 'R0' && oldelem.contains(elem)))
  if (!number)
    number = lastnumber
  elem = document.getElementById(prefix + number)
  doHighlight(prefix + number)
}
window.onload = scrollToBoth
window.onkeydown = handleKey
</script>
<body>
<div onclick='highlight(event)'>
)";

static void printHtml(raw_ostream &OS, char C) {
    switch (C) {
        case '&':
            OS << "&amp;";
            break;
        case '<':
            OS << "&lt;";
            break;
        case '>':
            OS << "&gt;";
            break;
        case '\'':
            OS << "&#x27;";
            break;
        case '"':
            OS << "&quot;";
            break;
        default:
            OS << C;
    }
}

static void printHtml(raw_ostream &OS, const StringRef Str) {
    for (char C : Str)
        printHtml(OS, C);
}

static std::string getChangeKindAbbr(diff::ChangeKind Kind) {
    switch (Kind) {
        case diff::NoChange:
            return "";
        case diff::Delete:
            return "d";
        case diff::Update:
            return "u";
        case diff::Insert:
            return "i";
        case diff::Move:
            return "m";
        case diff::UpdateMove:
            return "u m";
    }
    llvm_unreachable("Invalid enumeration value.");
}

static unsigned printHtmlForNode(raw_ostream &OS, const diff::ASTDiff &Diff,
                                 bool IsLeft, diff::NodeRef Node,
                                 unsigned Offset) {
    char MyTag, OtherTag;
    diff::NodeId LeftId, RightId;
    diff::SyntaxTree &Tree = Node.getTree();
    const SourceManager &SM = Tree.getASTContext().getSourceManager();
    SourceLocation SLoc = Node.getSourceRange().getBegin();
    if (SLoc.isValid() && !SM.isInMainFile(SLoc))
        return Offset;
    const diff::Node *Target = Diff.getMapped(Node);
    diff::NodeId TargetId = Target ? Target->getId() : diff::NodeId();
    if (IsLeft) {
        MyTag = 'L';
        OtherTag = 'R';
        LeftId = Node.getId();
        RightId = TargetId;
    } else {
        MyTag = 'R';
        OtherTag = 'L';
        LeftId = TargetId;
        RightId = Node.getId();
    }
    unsigned Begin, End;
    std::tie(Begin, End) = Node.getSourceRangeOffsets();
    auto Code = SM.getBufferOrNone(SM.getMainFileID())->getBuffer();
    for (; Offset < Begin; ++Offset)
        printHtml(OS, Code[Offset]);
    OS << "<span id='" << MyTag << Node.getId() << "' "
       << "tid='" << OtherTag << TargetId << "' ";
    OS << "title='";
    diff::ChangeKind Change = Diff.getNodeChange(Node);
    printHtml(OS, Node.getTypeLabel());
    OS << "\n" << LeftId << " -> " << RightId << "'";
    if (Change != diff::NoChange)
        OS << " class='" << getChangeKindAbbr(Change) << "'";
    OS << ">";

    for (diff::NodeRef Child : Node)
        Offset = printHtmlForNode(OS, Diff, IsLeft, Child, Offset);

    for (; Offset < End; ++Offset)
        printHtml(OS, Code[Offset]);
    if (&Node == &Tree.getRoot()) {
        End = Code.size();
        for (; Offset < End; ++Offset)
            printHtml(OS, Code[Offset]);
    }
    OS << "</span>";
    return Offset;
}

static void printJsonString(raw_ostream &OS, const StringRef Str) {
    for (signed char C : Str) {
        switch (C) {
            case '"':
                OS << R"(\")";
                break;
            case '\\':
                OS << R"(\\)";
                break;
            case '\n':
                OS << R"(\n)";
                break;
            case '\t':
                OS << R"(\t)";
                break;
            default:
                if ('\x00' <= C && C <= '\x1f') {
                    OS << R"(\u00)" << hexdigit(C >> 4) << hexdigit(C);
                } else {
                    OS << C;
                }
        }
    }
}

bool in_array(const std::string &value, const std::vector <std::string> &array) {
    return std::find(array.begin(), array.end(), value) != array.end();
}

static void printNodeAttributes(raw_ostream &OS, diff::SyntaxTree &Tree,
                                diff::NodeRef Node) {

    OS << R"("id":)" << int(Node.getId());
    if (Node.getParent())
        OS << R"(,"parent_id":)" << int(Node.getParent()->getId());
    OS << R"(,"type":")" << Node.getTypeLabel() << '"';

    if (Node.getTypeLabel() == "FunctionDecl" || Node.getTypeLabel() == "TypedefDecl" ||
        Node.getTypeLabel() == "RecordDecl" || Node.getTypeLabel() == "InitListExpr" || Node.getTypeLabel() == "FieldDecl"
        || Node.getTypeLabel() == "EnumDecl" || Node.getTypeLabel() == "EnumConstantDecl" ||
        Node.getTypeLabel() == "Macro" || Node.getTypeLabel() == "TypedefDecl" || Node.getTypeLabel() == "VarDecl"
        || Node.getTypeLabel() == "FileScopeAsmDecl") {
        std::string fileName = Node.getFileName();
        if (!fileName.empty()) {
            OS << R"(,"file":")";
            printJsonString(OS, fileName);
            OS << '"';
        }
    }

    if (Node.getTypeLabel() == "MemberExpr") {
        auto memNode = Node.ASTNode.get<MemberExpr>();
        if (memNode->isArrow()) {
            OS << R"(,"isArrow":")";
            printJsonString(OS, "yes");
            OS << '"';
        }
    }
    if (Node.getTypeLabel() == "VarDecl") {
        auto memNode = Node.ASTNode.get<VarDecl>();
        if (memNode->isStaticDataMember()) {
            OS << R"(,"isStatic":")";
            printJsonString(OS, "yes");
            OS << '"';
        } else{
            OS << R"(,"isStatic":")";
            printJsonString(OS, "no");
            OS << '"';
        }
    }
    std::string DataType = Node.getDataType();
    if (DataType != "") {
        OS << R"(,"data_type":")";
        printJsonString(OS, DataType);
        OS << R"(")";
    }


    auto Offsets = Node.getSourceRangeOffsets();
    auto StartLoc = Node.getSourceBeginLocation();
    auto EndLoc = Node.getSourceEndLocation();
    OS << R"(,"start line":)" << StartLoc.first;
    OS << R"(,"start column":)" << StartLoc.second;
    OS << R"(,"end line":)" << EndLoc.first;
    OS << R"(,"end column":)" << EndLoc.second;

    OS << R"(,"begin":)" << Offsets.first;
    OS << R"(,"end":)" << Offsets.second;


    std::string Value = Node.getValue();
    if (!Value.empty()) {
        OS << R"(,"value":")";
        printJsonString(OS, Value);
        OS << '"';
    }
}

static void printNodeAsJson(raw_ostream &OS, diff::SyntaxTree &Tree,
                            diff::NodeRef Node) {
    OS << "{";
    printNodeAttributes(OS, Tree, Node);
    auto Identifier = Node.getIdentifier();
    auto QualifiedIdentifier = Node.getQualifiedIdentifier();
    std::string RefType = Node.getRefType();

    if (Identifier) {
        OS << R"(,"identifier":")";
        printJsonString(OS, *Identifier);
        OS << R"(")";
        if (QualifiedIdentifier && *Identifier != *QualifiedIdentifier) {
            OS << R"(,"qualified_identifier":")";
            printJsonString(OS, *QualifiedIdentifier);
            OS << R"(")";
        }
    }

    if (!RefType.empty()) {
        OS << R"(,"ref_type":")";
        printJsonString(OS, RefType);
        OS << R"(")";
    }

    OS << R"(,"children":[)";
    auto ChildBegin = Node.begin(), ChildEnd = Node.end();
    if (ChildBegin != ChildEnd) {
        printNodeAsJson(OS, Tree, *ChildBegin);
        for (++ChildBegin; ChildBegin != ChildEnd; ++ChildBegin) {
            OS << ",";
            printNodeAsJson(OS, Tree, *ChildBegin);
        }
    }
    OS << "]}";
}

static void printTree(raw_ostream &OS, diff::SyntaxTree &Tree) {
    for (diff::NodeRef Node : Tree) {
        for (int I = 0; I < Node.Depth; ++I)
            OS << " ";
        Node.dump(OS);
        OS << "\n";
    }
}

int main(int argc, const char **argv) {
    std::string ErrorMessage;
    std::unique_ptr <CompilationDatabase> CommonCompilations =
            FixedCompilationDatabase::loadFromCommandLine(argc, argv, ErrorMessage);
    if (!CommonCompilations && !ErrorMessage.empty())
        llvm::errs() << ErrorMessage;
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



    if (ASTDump || ASTDumpJson) {
        addExtraArgs(CommonCompilations, "NONE");
        if (!DestinationPath.empty()) {
            llvm::errs() << "Error: Please specify exactly one filename.\n";
            return 1;
        }
        std::unique_ptr <ASTUnit> AST = getAST(CommonCompilations, SourcePath);
        if (!AST)
            return 1;
        diff::SyntaxTree Tree(*AST);
        if (ASTDump) {
            printTree(llvm::outs(), Tree);
            return 0;
        }
        llvm::outs() << R"({"filename":")";
        printJsonString(llvm::outs(), SourcePath);
        llvm::outs() << R"(","root":)";
        printNodeAsJson(llvm::outs(), Tree, Tree.getRoot());
        llvm::outs() << "}\n";
        return 0;
    }

    addExtraArgs(CommonCompilationsA, "A");
    addExtraArgs(CommonCompilationsC, "C");

    if (DestinationPath.empty()) {
        llvm::errs() << "Error: Exactly two paths are required.\n";
        return 1;
    }

    std::unique_ptr <ASTUnit> Src = getAST(CommonCompilationsA, SourcePath);
    std::unique_ptr <ASTUnit> Dst = getAST(CommonCompilationsC, DestinationPath);
    if (!Src || !Dst)
        return 1;

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
    diff::SyntaxTree SrcTree(*Src);
    diff::SyntaxTree DstTree(*Dst);


    diff::ASTDiff Diff(SrcTree, DstTree, Options);

    if (HtmlDiff) {
        llvm::outs() << HtmlDiffHeader << "<pre>";
        llvm::outs() << "<div id='L' class='code'>";
        printHtmlForNode(llvm::outs(), Diff, true, SrcTree.getRoot(), 0);
        llvm::outs() << "</div>";
        llvm::outs() << "<div id='R' class='code'>";
        printHtmlForNode(llvm::outs(), Diff, false, DstTree.getRoot(), 0);
        llvm::outs() << "</div>";
        llvm::outs() << "</pre></div></body></html>\n";
        return 0;
    }

    Diff.dumpChanges(llvm::outs(), PrintMatches);

    return 0;
}