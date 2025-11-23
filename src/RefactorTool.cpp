#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;  // Получаем SourceManager для проверки isInMainFile

    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("nonVirtualDtor")) {
        handle_nv_dtor(Dtor, Diag, SM);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("missingOverride")) {
        handle_miss_override(Method, Diag, SM);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("loopVar")) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(Dtor->getLocation())) {
        return;
    }
    unsigned LocationHash = Dtor->getLocation().getHashValue();
    if (virtualDtorLocations.find(LocationHash) != virtualDtorLocations.end()) {
        return;
    }

    SourceLocation InsertLoc = Dtor->getLocation();
    if (!InsertLoc.isValid()) {
        return;
    }

    Rewrite.InsertTextBefore(InsertLoc, "virtual ");
    virtualDtorLocations.insert(LocationHash);
    const unsigned DiagID =
        Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Добавлен virtual к деструктору базового класса");
    Diag.Report(Dtor->getLocation(), DiagID);
}

SourceLocation RefactorHandler::findClosingParenAfter(SourceLocation start, SourceManager &SM) {
    StringRef fileContent = SM.getBufferData(SM.getFileID(start));
    const char *bufferStart = fileContent.data();
    const char *current = SM.getCharacterData(start);
    const char *bufferEnd = bufferStart + fileContent.size();

    while (current < bufferEnd && (*current == ' ' || *current == '\t' || *current == '\n' || *current == '\r')) {
        current++;
    }

    while (current < bufferEnd) {
        if (*current == ')') {
            return start.getLocWithOffset(current - SM.getCharacterData(start) + 1);
        }
        current++;
    }

    return SourceLocation();
}

SourceLocation RefactorHandler::findLocationAfterParameters(const CXXMethodDecl *Method, SourceManager &SM) {
    if (Method->param_empty()) {
        // Если нет параметров, ищем закрывающую скобку после имени метода
        SourceLocation nameEnd = Method->getNameInfo().getEndLoc();
        if (!nameEnd.isValid())
            return SourceLocation();

        return findClosingParenAfter(nameEnd, SM);
    } else {
        // Если есть параметры, берем конец последнего параметра
        const ParmVarDecl *lastParam = Method->getParamDecl(Method->getNumParams() - 1);
        SourceLocation lastParamEnd = lastParam->getEndLoc();
        if (!lastParamEnd.isValid())
            return SourceLocation();

        return findClosingParenAfter(lastParamEnd, SM);
    }
}

void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(Method->getLocation())) {
        return;
    }
    if (Method->size_overridden_methods() == 0 || Method->hasAttr<OverrideAttr>()) {
        return;
    }
    SourceLocation InsertLoc = findLocationAfterParameters(Method, SM);
    if (!InsertLoc.isValid()) {
        return;
    }
    Rewrite.InsertTextAfter(InsertLoc, " override");
    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Добавлен override к методу '%0'");
    auto DB = Diag.Report(Method->getLocation(), DiagID);
    DB << Method->getNameAsString();
}

bool RefactorHandler::shouldAddReference(const VarDecl *LoopVar) {
    if (!LoopVar->getType().isConstQualified()) {
        return false;
    }

    if (LoopVar->getType()->isReferenceType()) {
        return false;
    }

    QualType baseType = LoopVar->getType().getNonReferenceType().getCanonicalType();
    if (baseType->isFundamentalType() || baseType->isPointerType()) {
        return false;
    }

    return true;
}

SourceLocation RefactorHandler::findTypeEndLocation(const VarDecl *LoopVar, SourceManager &SM) {
    TypeSourceInfo *typeSourceInfo = LoopVar->getTypeSourceInfo();
    if (!typeSourceInfo) {
        return SourceLocation();
    }

    TypeLoc typeLoc = typeSourceInfo->getTypeLoc();
    if (typeLoc.isNull()) {
        return SourceLocation();
    }

    SourceLocation typeEnd = typeLoc.getEndLoc();
    if (!typeEnd.isValid()) {
        return SourceLocation();
    }
    return Lexer::getLocForEndOfToken(typeEnd, 0, SM, Rewrite.getLangOpts());
}

void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(LoopVar->getLocation())) {
        return;
    }
    if (!shouldAddReference(LoopVar)) {
        return;
    }
    SourceLocation InsertLoc = findTypeEndLocation(LoopVar, SM);
    if (!InsertLoc.isValid()) {
        return;
    }
    Rewrite.InsertTextAfter(InsertLoc, "&");
    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Добавлена ссылка к переменной цикла '%0'");
    auto DB = Diag.Report(LoopVar->getLocation(), DiagID);
    DB << LoopVar->getNameAsString();
}

// todo: ниже необходимо реализовать матчеры для поиска узлов AST
// note: синтаксис написания матчеров точно такой же как и для использования clang-query
/*
    Пример того, как может выглядеть реализация:
    auto AllClassesMatcher()
    {
        return cxxRecordDecl().bind("classDecl");
    }
*/
auto NvDtorMatcher() {
    return cxxDestructorDecl(unless(isVirtual()),
                             hasParent(cxxRecordDecl(hasDescendant(cxxRecordDecl()),  // Класс имеет наследников
                                                     unless(isFinal())                // Исключаем final классы
                                                     )))
        .bind("classDecl");
}

auto NoOverrideMatcher() { return cxxMethodDecl(isOverride(), unless(isImplicit())).bind("methodDecl"); }

auto NoRefConstVarInRangeLoopMatcher() {
    return varDecl(hasParent(cxxForRangeStmt()), hasType(isConstQualified()), unless(hasType(referenceType())),
                   unless(hasType(isInteger())), unless(hasType(realFloatingPointType())),
                   unless(hasType(booleanType())))
        .bind("loopVar");
}

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    // Инициализируем Rewriter для рефакторинга.
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы данных.
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    // Создаем ClangTool
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    // Запускаем RefactorAction.
    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}