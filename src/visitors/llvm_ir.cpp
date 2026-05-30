#include "visitors/llvm_ir.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "parsing/ast.hpp"
#include "parsing/ast_traits.hpp"
#include "parsing/parse.hpp"
#include "semantics/symbol_table.hpp"
#include "semantics/type_table.hpp"
#include "util/box.hpp"
#include "util/overloaded.hpp"

namespace visitors {

namespace {

inline constexpr std::uint64_t kNodeBytes = 24;
inline constexpr std::uint64_t kPackedFieldBytes = 8;

struct TypedValue {
  llvm::Value* value = nullptr;
  semantics::Type type = semantics::intType();
};

struct ConstructorLayout {
  semantics::SymbolId data_type = semantics::kInvalidSymbolId;
  std::size_t tag = 0;
  // TODO возможно стоит не дублировать данные, а обращаться к таблице типов
  std::vector<semantics::Type> fields;
};

struct ApplicationChain {
  const ast::Expression* callee = nullptr;
  std::vector<const ast::Expression*> arguments;
};

struct TargetConfiguration {
  std::string triple;
  std::unique_ptr<llvm::TargetMachine> machine;
};

inline constexpr auto kLlvmObjectFileType =
    static_cast<llvm::CodeGenFileType>(1);

[[nodiscard]] std::string mangle(const semantics::Symbol& symbol) {
  return std::format("mf.{}.{}", symbol.id, symbol.name);
}

[[nodiscard]] ApplicationChain collectApplication(
    const ast::Application& application) {
  ApplicationChain chain;
  chain.arguments.push_back(application.argument.get());
  auto* current = application.function.get();

  while (true) {
    const auto* app = std::get_if<ast::Application>(current);
    if (app == nullptr) {
      break;
    }
    chain.arguments.push_back(app->argument.get());
    current = app->function.get();
  }

  std::ranges::reverse(chain.arguments);
  chain.callee = current;
  return chain;
}

void initializeLlvmTargets() {
  static std::once_flag flag;
  std::call_once(flag, [] {
    if (llvm::InitializeNativeTarget() != 0) {
      throw std::runtime_error("Cannot initialize native LLVM target");
    }
    if (llvm::InitializeNativeTargetAsmPrinter() != 0) {
      throw std::runtime_error("Cannot initialize native LLVM asm printer");
    }
  });
}

[[nodiscard]] auto codeGenOptimizationLevel(
    LlvmOptimizationLevel optimization) {
  switch (optimization) {
    case LlvmOptimizationLevel::O0:
      return *llvm::CodeGenOpt::getLevel(0);
    case LlvmOptimizationLevel::O1:
      return *llvm::CodeGenOpt::getLevel(1);
    case LlvmOptimizationLevel::O2:
      return *llvm::CodeGenOpt::getLevel(2);
    case LlvmOptimizationLevel::O3:
      return *llvm::CodeGenOpt::getLevel(3);
  }
  return *llvm::CodeGenOpt::getLevel(0);
}

[[nodiscard]] llvm::OptimizationLevel passOptimizationLevel(
    LlvmOptimizationLevel optimization) {
  switch (optimization) {
    case LlvmOptimizationLevel::O0:
      return llvm::OptimizationLevel::O0;
    case LlvmOptimizationLevel::O1:
      return llvm::OptimizationLevel::O1;
    case LlvmOptimizationLevel::O2:
      return llvm::OptimizationLevel::O2;
    case LlvmOptimizationLevel::O3:
      return llvm::OptimizationLevel::O3;
  }
  return llvm::OptimizationLevel::O0;
}

[[nodiscard]] TargetConfiguration createTargetConfiguration(
    const LlvmObjectOptions& options) {
  initializeLlvmTargets();

  auto triple = options.target_triple.empty()
                    ? llvm::sys::getDefaultTargetTriple()
                    : options.target_triple;
  std::string error;
#if LLVM_VERSION_MAJOR >= 22
  const auto llvm_triple = llvm::Triple{triple};
  const auto* target = llvm::TargetRegistry::lookupTarget(llvm_triple, error);
#else
  const auto* target = llvm::TargetRegistry::lookupTarget(triple, error);
#endif
  if (target == nullptr) {
    throw std::runtime_error(
        std::format("Cannot find LLVM target '{}': {}", triple, error));
  }

  llvm::TargetOptions target_options;
#if LLVM_VERSION_MAJOR >= 22
  auto machine =
      std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
          llvm_triple, "generic", "", target_options, llvm::Reloc::PIC_,
          std::nullopt, codeGenOptimizationLevel(options.optimization)));
#else
  auto machine =
      std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
          triple, "generic", "", target_options, llvm::Reloc::PIC_,
          std::nullopt, codeGenOptimizationLevel(options.optimization)));
#endif
  if (machine == nullptr) {
    throw std::runtime_error(
        std::format("Cannot create LLVM target machine for '{}'", triple));
  }

  return TargetConfiguration{
      .triple = std::move(triple),
      .machine = std::move(machine),
  };
}

class LlvmIrGenerator {
 public:
  LlvmIrGenerator(const parsing::ParsedProgram& parsed,
                  const semantics::AnalysisResult& analysis,
                  const semantics::TypeAnalysisResult& type_analysis,
                  const LlvmIrOptions& options)
      : parsed_(&parsed),
        analysis_(&analysis),
        type_analysis_(&type_analysis),
        module_(std::make_unique<llvm::Module>(options.module_name, context_)),
        builder_(context_),
        node_type_(llvm::StructType::create(context_, "mf.node")) {
    if (!options.target_triple.empty()) {
#if LLVM_VERSION_MAJOR >= 22
      module_->setTargetTriple(llvm::Triple{options.target_triple});
#else
      module_->setTargetTriple(options.target_triple);
#endif
    }
    if (!options.data_layout.empty()) {
      module_->setDataLayout(options.data_layout);
    }
    node_type_->setBody({i64Ty(), i64Ty(), ptrTy()});
  }

  [[nodiscard]] std::string generate() {
    generateModule();
    return printModule();
  }

  void writeObjectFile(llvm::TargetMachine& target_machine,
                       LlvmOptimizationLevel optimization,
                       const std::filesystem::path& output_path) {
    generateModule();
    optimizeModule(target_machine, optimization);
    emitObjectFile(target_machine, output_path);
  }

 private:
  void generateModule() {
    registerConstructors();
    declareRuntime();
    declareFunctions();
    defineFunctions();
    defineMain();
    verify();
  }

  // TODO заменить на ссылки ?
  const parsing::ParsedProgram* parsed_;
  const semantics::AnalysisResult* analysis_;
  const semantics::TypeAnalysisResult* type_analysis_;

  llvm::LLVMContext context_;
  std::unique_ptr<llvm::Module> module_;
  llvm::IRBuilder<> builder_;
  llvm::StructType* node_type_ = nullptr;
  llvm::FunctionCallee malloc_;
  llvm::FunctionCallee printf_;
  llvm::FunctionCallee trap_;

  std::unordered_map<semantics::SymbolId, llvm::Function*> functions_;
  std::unordered_map<semantics::SymbolId, ConstructorLayout> constructors_;
  std::unordered_map<semantics::SymbolId, TypedValue> locals_;

  [[nodiscard]] const semantics::ScopeTree& scopes() const {
    return analysis_->scopes;
  }

  [[nodiscard]] const semantics::TypeTable& types() const {
    return type_analysis_->types;
  }

  [[nodiscard]] const semantics::Symbol* getRootSymbol(
      std::string_view name, semantics::SymbolKind kind) const {
    const auto* symbol =
        scopes().getLocalSymbol(semantics::ScopeTree::getRootScopeId(), name);
    if (symbol == nullptr || symbol->kind != kind) {
      return nullptr;
    }
    return symbol;
  }

  [[nodiscard]] const semantics::Symbol* getSymbolInNodeScope(
      ast::NodeId node_id, std::string_view name,
      semantics::SymbolKind kind) const {
    const auto scope_id = scopes().getScopeId(node_id);
    if (!scope_id.has_value()) {
      return nullptr;
    }

    const auto* symbol = scopes().getLocalSymbol(*scope_id, name);
    if (symbol == nullptr || symbol->kind != kind) {
      return nullptr;
    }
    return symbol;
  }

  [[nodiscard]] llvm::Type* i32Ty() { return builder_.getInt32Ty(); }

  [[nodiscard]] llvm::IntegerType* i64Ty() { return builder_.getInt64Ty(); }

  [[nodiscard]] llvm::Type* voidTy() { return builder_.getVoidTy(); }

  [[nodiscard]] llvm::PointerType* ptrTy() { return builder_.getPtrTy(); }

  [[nodiscard]] llvm::ConstantInt* i64(std::size_t value) {
    return builder_.getInt64(static_cast<std::uint64_t>(value));
  }

  [[nodiscard]] llvm::Type* llvmType(const semantics::Type& type) {
    return std::visit(
        util::overloaded{
            [&](const semantics::IntType&) -> llvm::Type* { return i64Ty(); },
            [&](const semantics::DataType&) -> llvm::Type* { return ptrTy(); },
            [&](const Box<semantics::FunctionType>&) -> llvm::Type* {
              throw std::runtime_error(
                  "Higher-order LLVM lowering is not supported");
            },
        },
        type);
  }

  void registerConstructors() {
    std::size_t tag = 0;
    for (const auto& definition : parsed_->ast.definitions) {
      const auto* data_definition =
          std::get_if<ast::DataTypeDefinition>(&definition);
      if (data_definition == nullptr) {
        continue;
      }

      const auto* data_symbol =
          getRootSymbol(data_definition->name, semantics::SymbolKind::DataType);
      if (data_symbol == nullptr) {
        continue;
      }

      for (const auto& constructor : data_definition->constructors) {
        const auto* constructor_symbol =
            getRootSymbol(constructor.name, semantics::SymbolKind::Constructor);
        if (constructor_symbol == nullptr) {
          continue;
        }

        const auto* signature =
            types().getConstructorSignature(constructor_symbol->id);
        if (signature == nullptr) {
          continue;
        }

        constructors_.emplace(constructor_symbol->id,
                              ConstructorLayout{
                                  .data_type = data_symbol->id,
                                  .tag = tag,
                                  .fields = signature->fields,
                              });
        ++tag;
      }
    }
  }

  // TODO освобождать память через free как-нибудь потом
  void declareRuntime() {
    malloc_ = module_->getOrInsertFunction(
        "malloc", llvm::FunctionType::get(ptrTy(), {i64Ty()}, false));
    printf_ = module_->getOrInsertFunction(
        "printf", llvm::FunctionType::get(i32Ty(), {ptrTy()}, true));
    trap_ = module_->getOrInsertFunction(
        "llvm.trap", llvm::FunctionType::get(voidTy(), false));
  }

  void declareFunctions() {
    for (const auto& definition : parsed_->ast.definitions) {
      const auto* function = std::get_if<ast::FunctionDefinition>(&definition);
      if (function == nullptr) {
        continue;
      }

      const auto* symbol =
          getRootSymbol(function->name, semantics::SymbolKind::Function);
      if (symbol == nullptr) {
        continue;
      }

      const auto* signature = types().getFunctionSignature(symbol->id);
      if (signature == nullptr) {
        continue;
      }

      std::vector<llvm::Type*> parameters;
      parameters.reserve(signature->parameters.size());
      for (const auto& parameter : signature->parameters) {
        parameters.push_back(llvmType(parameter));
      }

      auto* function_type = llvm::FunctionType::get(llvmType(signature->result),
                                                    parameters, false);
      auto* llvm_function =
          llvm::Function::Create(function_type, llvm::Function::ExternalLinkage,
                                 mangle(*symbol), module_.get());

      for (std::size_t index = 0; index < function->parameters.size();
           ++index) {
        llvm_function->getArg(index)->setName(function->parameters.at(index));
      }

      functions_.emplace(symbol->id, llvm_function);
    }
  }

  void defineFunctions() {
    for (const auto& definition : parsed_->ast.definitions) {
      const auto* function = std::get_if<ast::FunctionDefinition>(&definition);
      if (function != nullptr) {
        defineFunction(*function);
      }
    }
  }

  void defineFunction(const ast::FunctionDefinition& function) {
    const auto* symbol =
        getRootSymbol(function.name, semantics::SymbolKind::Function);
    if (symbol == nullptr) {
      return;
    }

    auto function_it = functions_.find(symbol->id);
    if (function_it == functions_.end()) {
      return;
    }

    const auto* signature = types().getFunctionSignature(symbol->id);
    if (signature == nullptr) {
      return;
    }

    locals_.clear();
    const auto function_scope = scopes().getScopeId(function.id);
    if (!function_scope.has_value()) {
      throw std::runtime_error(
          std::format("Function '{}' has no semantic scope", function.name));
    }

    auto* llvm_function = function_it->second;
    auto* entry = llvm::BasicBlock::Create(context_, "entry", llvm_function);
    builder_.SetInsertPoint(entry);

    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      const auto& parameter = function.parameters.at(index);
      const auto& type = signature->parameters.at(index);
      const auto* parameter_symbol =
          scopes().getLocalSymbol(*function_scope, parameter);
      if (parameter_symbol == nullptr) {
        throw std::runtime_error(
            std::format("Parameter '{}' has no semantic binding", parameter));
      }
      locals_[parameter_symbol->id] = TypedValue{
          .value = llvm_function->getArg(index),
          .type = type,
      };
    }

    auto body = emitExpression(*function.body);
    builder_.CreateRet(body.value);
  }

  void defineMain() {
    const auto* main_symbol =
        getRootSymbol("main", semantics::SymbolKind::Function);
    if (main_symbol == nullptr) {
      throw std::runtime_error("No 'main' function defined");
    }

    auto function_it = functions_.find(main_symbol->id);
    if (function_it == functions_.end()) {
      throw std::runtime_error("No LLVM declaration for 'main'");
    }

    const auto* signature = types().getFunctionSignature(main_symbol->id);
    if (signature == nullptr || !signature->parameters.empty()) {
      throw std::runtime_error("Invalid MF main signature");
    }

    auto* main_type = llvm::FunctionType::get(i32Ty(), false);
    auto* main_function = llvm::Function::Create(
        main_type, llvm::Function::ExternalLinkage, "main", module_.get());
    auto* entry = llvm::BasicBlock::Create(context_, "entry", main_function);
    builder_.SetInsertPoint(entry);

    auto* result = builder_.CreateCall(function_it->second, {});
    emitPrint(result, signature->result);
    builder_.CreateRet(builder_.getInt32(0));
  }

  void emitPrint(llvm::Value* value, const semantics::Type& type) {
    std::visit(util::overloaded{
                   [&](const semantics::IntType&) {
                     auto* format = builder_.CreateGlobalString("%ld\n");
                     builder_.CreateCall(printf_, {format, value});
                   },
                   [&](const semantics::DataType&) {
                     auto* format = builder_.CreateGlobalString("%ld\n");
                     builder_.CreateCall(printf_, {format, loadTag(value)});
                   },
                   [&](const Box<semantics::FunctionType>&) {
                     throw std::runtime_error("Cannot print function value");
                   },
               },
               type);
  }

  [[nodiscard]] TypedValue emitExpression(const ast::Expression& expression) {
    return std::visit(util::overloaded{
                          [&](const ast::IntLiteral& literal) {
                            return TypedValue{
                                .value = builder_.getInt64(literal.value),
                                .type = semantics::intType(),
                            };
                          },
                          [&](const ast::Variable& variable) {
                            return emitVariable(variable);
                          },
                          [&]<ast::IsBinaryOperator Op>(const Op& op) {
                            return emitBinaryOperator(op);
                          },
                          [&](const ast::Application& application) {
                            return emitApplication(application);
                          },
                          [&](const ast::CaseExpression& case_expression) {
                            return emitCaseExpression(case_expression);
                          },
                      },
                      expression);
  }

  [[nodiscard]] TypedValue emitVariable(const ast::Variable& variable) {
    const auto* symbol = scopes().getResolvedSymbol(variable.id);
    if (symbol == nullptr) {
      throw std::runtime_error(
          std::format("Variable '{}' has no semantic binding", variable.name));
    }

    if (symbol->kind == semantics::SymbolKind::Constructor) {
      const auto layout_it = constructors_.find(symbol->id);
      if (layout_it == constructors_.end()) {
        throw std::runtime_error(
            std::format("Constructor '{}' has no layout", symbol->name));
      }
      if (!layout_it->second.fields.empty()) {
        throw std::runtime_error(
            std::format("Constructor '{}' needs arguments", symbol->name));
      }
      return emitConstructedValue(*symbol, layout_it->second, {});
    }

    if (symbol->kind == semantics::SymbolKind::Function) {
      throw std::runtime_error(
          std::format("Function '{}' cannot be used as a value", symbol->name));
    }

    auto it = locals_.find(symbol->id);
    if (it == locals_.end()) {
      throw std::runtime_error(
          std::format("Local '{}' has no LLVM value", symbol->name));
    }
    return it->second;
  }

  template <ast::IsBinaryOperator Op>
  [[nodiscard]] TypedValue emitBinaryOperator(const Op& op) {
    auto left = emitExpression(*op.left_operand);
    auto right = emitExpression(*op.right_operand);

    llvm::Value* value = nullptr;
    switch (ast::kindOf<Op>()) {
      case ast::BinaryOperatorKind::Addition:
        value = builder_.CreateAdd(left.value, right.value, "add");
        break;
      case ast::BinaryOperatorKind::Subtraction:
        value = builder_.CreateSub(left.value, right.value, "sub");
        break;
      case ast::BinaryOperatorKind::Multiplication:
        value = builder_.CreateMul(left.value, right.value, "mul");
        break;
      case ast::BinaryOperatorKind::Division:
        value = builder_.CreateSDiv(left.value, right.value, "div");
        break;
    }

    return TypedValue{
        .value = value,
        .type = semantics::intType(),
    };
  }

  [[nodiscard]] TypedValue emitApplication(
      const ast::Application& application) {
    const auto chain = collectApplication(application);
    const auto* callee = std::get_if<ast::Variable>(chain.callee);
    if (callee == nullptr) {
      throw std::runtime_error(
          "Only named functions and constructors are callable");
    }

    const auto* symbol = scopes().getResolvedSymbol(callee->id);
    if (symbol == nullptr) {
      throw std::runtime_error(
          std::format("Callable '{}' has no semantic binding", callee->name));
    }

    if (symbol->kind == semantics::SymbolKind::Function) {
      return emitFunctionCall(*symbol, chain.arguments);
    }
    if (symbol->kind == semantics::SymbolKind::Constructor) {
      return emitConstructorCall(*symbol, chain.arguments);
    }

    throw std::runtime_error(std::format("'{}' is not callable", symbol->name));
  }

  [[nodiscard]] TypedValue emitFunctionCall(
      const semantics::Symbol& symbol,
      const std::vector<const ast::Expression*>& arguments) {
    auto function_it = functions_.find(symbol.id);
    if (function_it == functions_.end()) {
      throw std::runtime_error(
          std::format("Function '{}' has no LLVM declaration", symbol.name));
    }

    const auto* signature = types().getFunctionSignature(symbol.id);
    if (signature == nullptr ||
        signature->parameters.size() != arguments.size()) {
      throw std::runtime_error(
          std::format("Function '{}' called with invalid arity", symbol.name));
    }

    std::vector<llvm::Value*> llvm_arguments;
    llvm_arguments.reserve(arguments.size());
    for (const auto* argument : arguments) {
      llvm_arguments.push_back(emitExpression(*argument).value);
    }

    return TypedValue{
        .value = builder_.CreateCall(function_it->second, llvm_arguments,
                                     std::format("call.{}", symbol.name)),
        .type = signature->result,
    };
  }

  [[nodiscard]] TypedValue emitConstructorCall(
      const semantics::Symbol& symbol,
      const std::vector<const ast::Expression*>& arguments) {
    auto layout_it = constructors_.find(symbol.id);
    if (layout_it == constructors_.end()) {
      throw std::runtime_error(
          std::format("Constructor '{}' has no layout", symbol.name));
    }
    if (layout_it->second.fields.size() != arguments.size()) {
      throw std::runtime_error(std::format(
          "Constructor '{}' expects {} arguments, got {}", symbol.name,
          layout_it->second.fields.size(), arguments.size()));
    }

    std::vector<TypedValue> emitted_arguments;
    emitted_arguments.reserve(arguments.size());
    for (const auto* argument : arguments) {
      emitted_arguments.push_back(emitExpression(*argument));
    }

    return emitConstructedValue(symbol, layout_it->second,
                                std::move(emitted_arguments));
  }

  [[nodiscard]] TypedValue emitConstructedValue(
      [[maybe_unused]] const semantics::Symbol& symbol,
      const ConstructorLayout& layout, std::vector<TypedValue> arguments) {
    auto* node = builder_.CreateCall(malloc_, {i64(kNodeBytes)}, "node");
    builder_.CreateStore(i64(layout.tag),
                         builder_.CreateStructGEP(node_type_, node, 0));
    builder_.CreateStore(i64(arguments.size()),
                         builder_.CreateStructGEP(node_type_, node, 1));

    llvm::Value* fields = llvm::ConstantPointerNull::get(ptrTy());
    if (!arguments.empty()) {
      auto* bytes = builder_.CreateMul(i64(arguments.size()),
                                       i64(kPackedFieldBytes), "fields.bytes");
      fields = builder_.CreateCall(malloc_, {bytes}, "fields");
      for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments.at(index);
        auto* slot = builder_.CreateInBoundsGEP(i64Ty(), fields, i64(index));
        builder_.CreateStore(packValue(argument.value, layout.fields.at(index)),
                             slot);
      }
    }

    builder_.CreateStore(fields, builder_.CreateStructGEP(node_type_, node, 2));
    return TypedValue{
        .value = node,
        .type = semantics::dataType(layout.data_type),
    };
  }

  [[nodiscard]] llvm::Value* packValue(llvm::Value* value,
                                       const semantics::Type& type) {
    return std::visit(
        util::overloaded{
            [&](const semantics::IntType&) -> llvm::Value* { return value; },
            [&](const semantics::DataType&) -> llvm::Value* {
              return builder_.CreatePtrToInt(value, i64Ty(), "field.ptrint");
            },
            [&](const Box<semantics::FunctionType>&) -> llvm::Value* {
              throw std::runtime_error("Cannot store function field");
            },
        },
        type);
  }

  [[nodiscard]] llvm::Value* unpackValue(llvm::Value* value,
                                         const semantics::Type& type) {
    return std::visit(
        util::overloaded{
            [&](const semantics::IntType&) -> llvm::Value* { return value; },
            [&](const semantics::DataType&) -> llvm::Value* {
              return builder_.CreateIntToPtr(value, ptrTy(), "field.ptr");
            },
            [&](const Box<semantics::FunctionType>&) -> llvm::Value* {
              throw std::runtime_error("Cannot load function field");
            },
        },
        type);
  }

  [[nodiscard]] llvm::Value* loadTag(llvm::Value* node) {
    auto* tag_ptr = builder_.CreateStructGEP(node_type_, node, 0);
    return builder_.CreateLoad(i64Ty(), tag_ptr, "tag");
  }

  [[nodiscard]] llvm::Value* loadField(llvm::Value* node, std::size_t index,
                                       const semantics::Type& type) {
    auto* fields_ptr = builder_.CreateStructGEP(node_type_, node, 2);
    auto* fields = builder_.CreateLoad(ptrTy(), fields_ptr, "fields");
    auto* field_ptr = builder_.CreateInBoundsGEP(i64Ty(), fields, i64(index));
    auto* packed = builder_.CreateLoad(i64Ty(), field_ptr, "field");
    return unpackValue(packed, type);
  }

  [[nodiscard]] bool canUseSwitchCase(
      const ast::CaseExpression& case_expression) {
    std::unordered_set<semantics::SymbolId> constructor_ids;
    for (const auto& branch : case_expression.branches) {
      const auto* pattern =
          std::get_if<ast::ConstructorPattern>(&branch.pattern);
      if (pattern == nullptr) {
        return false;
      }

      const auto* constructor = scopes().getResolvedSymbol(pattern->id);
      if (constructor == nullptr) {
        return false;
      }
      if (!constructor_ids.insert(constructor->id).second) {
        return false;
      }
    }
    return true;
  }

  void emitTrap() {
    builder_.CreateCall(trap_, {});
    builder_.CreateUnreachable();
  }

  [[nodiscard]] TypedValue finishCaseExpression(
      llvm::BasicBlock* merge_block,
      const std::vector<std::pair<TypedValue, llvm::BasicBlock*>>& incoming) {
    if (incoming.empty()) {
      throw std::runtime_error("Case expression has no branches");
    }

    builder_.SetInsertPoint(merge_block);
    auto result = incoming.front().first;
    // TODO использовать передачу переменных через память вместо phi-инструкций
    auto* phi = builder_.CreatePHI(llvmType(result.type), incoming.size(),
                                   "case.result");
    for (const auto& [typed_value, block] : incoming) {
      phi->addIncoming(typed_value.value, block);
    }
    result.value = phi;
    return result;
  }

  [[nodiscard]] TypedValue emitSwitchCaseExpression(
      const ast::CaseExpression& case_expression, const TypedValue& scrutinee) {
    auto* function = builder_.GetInsertBlock()->getParent();
    auto* default_block =
        llvm::BasicBlock::Create(context_, "case.default", function);
    auto* merge_block =
        llvm::BasicBlock::Create(context_, "case.merge", function);
    auto* switch_inst =
        builder_.CreateSwitch(loadTag(scrutinee.value), default_block,
                              case_expression.branches.size());

    std::vector<std::pair<TypedValue, llvm::BasicBlock*>> incoming;
    for (const auto& branch : case_expression.branches) {
      const auto& pattern = std::get<ast::ConstructorPattern>(branch.pattern);
      const auto* constructor = scopes().getResolvedSymbol(pattern.id);
      if (constructor == nullptr) {
        throw std::runtime_error(std::format(
            "Pattern '{}' has no constructor binding", pattern.name));
      }

      const auto layout_it = constructors_.find(constructor->id);
      if (layout_it == constructors_.end()) {
        throw std::runtime_error(
            std::format("Constructor '{}' has no layout", constructor->name));
      }
      const auto& layout = layout_it->second;

      auto* branch_block =
          llvm::BasicBlock::Create(context_, "case.ctor", function);
      switch_inst->addCase(i64(layout.tag), branch_block);

      builder_.SetInsertPoint(branch_block);
      auto saved_locals = locals_;
      emitConstructorPattern(pattern, scrutinee.value, layout);
      emitBranchBody(branch, merge_block, incoming);
      locals_ = std::move(saved_locals);
    }

    builder_.SetInsertPoint(default_block);
    emitTrap();
    return finishCaseExpression(merge_block, incoming);
  }

  [[nodiscard]] TypedValue emitCaseExpression(
      const ast::CaseExpression& case_expression) {
    auto scrutinee = emitExpression(*case_expression.scrutinee);
    if (canUseSwitchCase(case_expression)) {
      return emitSwitchCaseExpression(case_expression, scrutinee);
    }

    auto* function = builder_.GetInsertBlock()->getParent();
    auto* merge_block =
        llvm::BasicBlock::Create(context_, "case.merge", function);

    std::vector<std::pair<TypedValue, llvm::BasicBlock*>> incoming;
    auto has_fallthrough = true;
    for (const auto& branch : case_expression.branches) {
      if (!has_fallthrough) {
        break;
      }

      has_fallthrough =
          emitSequentialCaseBranch(branch, scrutinee, merge_block, incoming);
    }

    if (has_fallthrough) {
      emitTrap();
    }

    return finishCaseExpression(merge_block, incoming);
  }

  [[nodiscard]] bool emitSequentialCaseBranch(
      const ast::Branch& branch, const TypedValue& scrutinee,
      llvm::BasicBlock* merge_block,
      std::vector<std::pair<TypedValue, llvm::BasicBlock*>>& incoming) {
    return std::visit(util::overloaded{
                          [&](const ast::VariablePattern& pattern) {
                            emitVariablePattern(pattern, scrutinee);
                            emitBranchBody(branch, merge_block, incoming);
                            return false;
                          },
                          [&](const ast::ConstructorPattern& pattern) {
                            emitConstructorCaseBranch(pattern, branch,
                                                      scrutinee, merge_block,
                                                      incoming);
                            return true;
                          },
                      },
                      branch.pattern);
  }

  void emitConstructorCaseBranch(
      const ast::ConstructorPattern& pattern, const ast::Branch& branch,
      const TypedValue& scrutinee, llvm::BasicBlock* merge_block,
      std::vector<std::pair<TypedValue, llvm::BasicBlock*>>& incoming) {
    const auto* constructor = scopes().getResolvedSymbol(pattern.id);
    if (constructor == nullptr) {
      throw std::runtime_error(
          std::format("Pattern '{}' has no constructor binding", pattern.name));
    }

    const auto layout_it = constructors_.find(constructor->id);
    if (layout_it == constructors_.end()) {
      throw std::runtime_error(
          std::format("Constructor '{}' has no layout", constructor->name));
    }
    const auto& layout = layout_it->second;

    auto* function = builder_.GetInsertBlock()->getParent();
    auto* matched_block =
        llvm::BasicBlock::Create(context_, "case.matched", function);
    auto* next_block =
        llvm::BasicBlock::Create(context_, "case.next", function);

    auto* matched = builder_.CreateICmpEQ(loadTag(scrutinee.value),
                                          i64(layout.tag), "case.match");
    builder_.CreateCondBr(matched, matched_block, next_block);

    builder_.SetInsertPoint(matched_block);
    auto saved_locals = locals_;
    emitConstructorPattern(pattern, scrutinee.value, layout);
    emitBranchBody(branch, merge_block, incoming);
    locals_ = std::move(saved_locals);

    builder_.SetInsertPoint(next_block);
  }

  void emitVariablePattern(const ast::VariablePattern& pattern,
                           const TypedValue& scrutinee) {
    const auto* symbol = getSymbolInNodeScope(
        pattern.id, pattern.name, semantics::SymbolKind::PatternVariable);
    if (symbol == nullptr) {
      throw std::runtime_error(
          std::format("Pattern '{}' has no semantic binding", pattern.name));
    }
    locals_[symbol->id] = scrutinee;
  }

  void emitConstructorPattern(const ast::ConstructorPattern& pattern,
                              llvm::Value* scrutinee,
                              const ConstructorLayout& layout) {
    if (pattern.arguments.size() != layout.fields.size()) {
      throw std::runtime_error(
          std::format("Pattern '{}' has invalid bindings", pattern.name));
    }

    const auto scope_id = scopes().getScopeId(pattern.id);
    if (!scope_id.has_value()) {
      throw std::runtime_error(
          std::format("Pattern '{}' has invalid bindings", pattern.name));
    }

    for (std::size_t index = 0; index < pattern.arguments.size(); ++index) {
      const auto* binding =
          scopes().getLocalSymbol(*scope_id, pattern.arguments.at(index));
      if (binding == nullptr ||
          binding->kind != semantics::SymbolKind::PatternVariable) {
        throw std::runtime_error(
            std::format("Pattern '{}' has invalid bindings", pattern.name));
      }
      locals_[binding->id] = TypedValue{
          .value = loadField(scrutinee, index, layout.fields.at(index)),
          .type = layout.fields.at(index),
      };
    }
  }

  void emitBranchBody(
      const ast::Branch& branch, llvm::BasicBlock* merge_block,
      std::vector<std::pair<TypedValue, llvm::BasicBlock*>>& incoming) {
    auto body = emitExpression(*branch.body);
    auto* incoming_block = builder_.GetInsertBlock();
    if (incoming_block->getTerminator() == nullptr) {
      builder_.CreateBr(merge_block);
    }
    incoming.emplace_back(body, incoming_block);
  }

  void optimizeModule(llvm::TargetMachine& target_machine,
                      LlvmOptimizationLevel optimization) {
    if (optimization == LlvmOptimizationLevel::O0) {
      return;
    }

    llvm::LoopAnalysisManager loop_analysis_manager;
    llvm::FunctionAnalysisManager function_analysis_manager;
    llvm::CGSCCAnalysisManager cgscc_analysis_manager;
    llvm::ModuleAnalysisManager module_analysis_manager;

    llvm::PassBuilder pass_builder{&target_machine};
    pass_builder.registerModuleAnalyses(module_analysis_manager);
    pass_builder.registerCGSCCAnalyses(cgscc_analysis_manager);
    pass_builder.registerFunctionAnalyses(function_analysis_manager);
    pass_builder.registerLoopAnalyses(loop_analysis_manager);
    pass_builder.crossRegisterProxies(
        loop_analysis_manager, function_analysis_manager,
        cgscc_analysis_manager, module_analysis_manager);

    auto module_pass_manager = pass_builder.buildPerModuleDefaultPipeline(
        passOptimizationLevel(optimization));
    module_pass_manager.run(*module_, module_analysis_manager);
    verify();
  }

  void emitObjectFile(llvm::TargetMachine& target_machine,
                      const std::filesystem::path& output_path) {
    const auto parent = output_path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }

    std::error_code error;
    llvm::raw_fd_ostream destination{output_path.string(), error,
                                     llvm::sys::fs::OF_None};
    if (error) {
      throw std::runtime_error(std::format("Cannot open '{}' for writing: {}",
                                           output_path.string(),
                                           error.message()));
    }

    llvm::legacy::PassManager pass_manager;
    if (target_machine.addPassesToEmitFile(pass_manager, destination, nullptr,
                                           kLlvmObjectFileType)) {
      throw std::runtime_error("LLVM target machine cannot emit object files");
    }

    pass_manager.run(*module_);
    destination.flush();
  }

  void verify() {
    std::string message;
    llvm::raw_string_ostream stream(message);
    if (llvm::verifyModule(*module_, &stream)) {
      stream.flush();
      throw std::runtime_error(std::format("Invalid LLVM IR: {}", message));
    }
  }

  [[nodiscard]] std::string printModule() {
    std::string ir;
    llvm::raw_string_ostream stream(ir);
    module_->print(stream, nullptr);
    stream.flush();
    return ir;
  }
};

void throwFirstDiagnostic(
    const parsing::ParsedProgram& parsed,
    const std::vector<semantics::Diagnostic>& diagnostics) {
  if (!diagnostics.empty()) {
    throw std::runtime_error(
        semantics::formatDiagnostic(parsed, diagnostics.front()));
  }
}

[[nodiscard]] std::pair<semantics::AnalysisResult,
                        semantics::TypeAnalysisResult>
analyzeOrThrow(const parsing::ParsedProgram& parsed) {
  auto analysis = semantics::analyze(parsed);
  if (!analysis.ok()) {
    throwFirstDiagnostic(parsed, analysis.diagnostics);
  }

  auto type_analysis = semantics::analyzeTypes(parsed, analysis);
  if (!type_analysis.ok()) {
    throwFirstDiagnostic(parsed, type_analysis.diagnostics);
  }

  return {std::move(analysis), std::move(type_analysis)};
}

[[nodiscard]] std::string generateCheckedLlvmIr(
    const parsing::ParsedProgram& parsed,
    const semantics::AnalysisResult& analysis,
    const semantics::TypeAnalysisResult& type_analysis,
    const LlvmIrOptions& options) {
  return LlvmIrGenerator{parsed, analysis, type_analysis, options}.generate();
}

}  // namespace

// TODO семантический анализ отдельно, передавать пару таблиц в ллвм ир
std::string generateLlvmIr(const parsing::ParsedProgram& parsed,
                           const LlvmIrOptions& options) {
  const auto [analysis, type_analysis] = analyzeOrThrow(parsed);
  return generateCheckedLlvmIr(parsed, analysis, type_analysis, options);
}

std::filesystem::path writeLlvmIr(const parsing::ParsedProgram& parsed,
                                  const LlvmIrOptions& options) {
  const auto [analysis, type_analysis] = analyzeOrThrow(parsed);
  const auto output_path = options.output_path;
  auto ir = generateCheckedLlvmIr(parsed, analysis, type_analysis, options);
  const auto parent = output_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  std::ofstream output(output_path);
  if (!output) {
    throw std::runtime_error(
        std::format("Cannot open '{}' for writing", output_path.string()));
  }
  output << ir;
  return output_path;
}

std::string defaultTargetTriple() {
  return llvm::sys::getDefaultTargetTriple();
}

std::filesystem::path writeObjectFile(const parsing::ParsedProgram& parsed,
                                      const LlvmObjectOptions& options) {
  const auto [analysis, type_analysis] = analyzeOrThrow(parsed);
  auto target = createTargetConfiguration(options);
  const auto data_layout =
      target.machine->createDataLayout().getStringRepresentation();

  LlvmIrGenerator{parsed, analysis, type_analysis,
                  LlvmIrOptions{
                      .module_name = options.module_name,
                      .output_path = options.output_path,
                      .target_triple = target.triple,
                      .data_layout = data_layout,
                  }}
      .writeObjectFile(*target.machine, options.optimization,
                       options.output_path);
  return options.output_path;
}

}  // namespace visitors
