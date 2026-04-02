#include "parsing/parse.hpp"

#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "parsing/ast.hpp"
#include "util/box.hpp"
#include "util/position.hpp"

namespace parsing {

namespace {

class Parser {
 public:
  explicit Parser(const std::vector<tokenization::Token>& tokens)
      : it_(tokens.begin()), end_(tokens.end()) {}

  ParsedProgram parse() {
    ast::Program program;
    while (!atEnd()) {
      program.definitions.push_back(parseDefinition());
    }

    return {
        std::move(program),
        std::move(positions_),
    };
  }

 private:
  using TokenIterator = std::vector<tokenization::Token>::const_iterator;
  using BinaryOperatorFactory = ast::Expression (*)(ast::NodeId,
                                                    ast::ExpressionPtr,
                                                    ast::ExpressionPtr);

  TokenIterator it_;
  const TokenIterator end_;  // NOLINT
  std::vector<util::Position> positions_;

  [[nodiscard]] bool atEnd() const { return it_ == end_; }

  [[nodiscard]] ast::NodeId allocateNode(const util::Position& position) {
    if (positions_.size() >= ast::kInvalidNodeId) {
      throw std::overflow_error("Too many AST nodes");
    }

    const auto node_id = positions_.size();
    positions_.push_back(position);
    return node_id;
  }

  const tokenization::Token& advance() {
    const auto& token = *it_;
    ++it_;
    return token;
  }

  template <typename T>
  [[nodiscard]] bool check() const {
    return !atEnd() && std::holds_alternative<T>(it_->load);
  }

  template <typename... Ts>
  [[nodiscard]] bool checkAny() const {
    return (check<Ts>() || ...);
  }

  template <typename T>
  const T& expect() {
    if (!check<T>()) {
      if (atEnd()) {
        throw std::runtime_error("Unexpected end of input");
      }
      throw std::runtime_error(
          std::format("Unexpected token at {}", it_->position.toString()));
    }
    return std::get<T>(advance().load);
  }

  template <typename T>
  bool match() {
    if (check<T>()) {
      advance();
      return true;
    }
    return false;
  }

  [[noreturn]] void throwExpected(std::string_view expected) const {
    if (atEnd()) {
      throw std::runtime_error(
          std::format("Expected {}, got end of input", expected));
    }
    throw std::runtime_error(
        std::format("Expected {} at {}", expected, it_->position.toString()));
  }

  template <typename Token>
  std::string expectName() {
    return std::string(expect<Token>().name);
  }

  template <typename First, typename... Rest>
  std::string expectAnyName() {
    if (check<First>()) {
      return expectName<First>();
    }
    if constexpr (sizeof...(Rest) > 0) {
      return expectAnyName<Rest...>();
    }
    throwExpected("name");
  }

  template <typename Token>
  std::vector<std::string> parseNamesWhile() {
    std::vector<std::string> names;
    while (check<Token>()) {
      names.push_back(expectName<Token>());
    }
    return names;
  }

  // --- Definitions ---

  ast::Definition parseDefinition() {
    if (check<tokenization::Function>()) {
      return parseFunctionDefinition();
    }
    if (check<tokenization::Data>()) {
      return parseDataTypeDefinition();
    }
    throwExpected("'defn' or 'data'");
  }

  ast::FunctionDefinition parseFunctionDefinition() {
    const auto& position = it_->position;
    expect<tokenization::Function>();
    auto name = expectName<tokenization::LowerVariable>();
    auto parameters = parseNamesWhile<tokenization::LowerVariable>();

    expect<tokenization::Equal>();
    expect<tokenization::LeftBrace>();
    auto body = parseExpression();
    expect<tokenization::RightBrace>();

    return {
        allocateNode(position),
        std::move(name),
        std::move(parameters),
        std::move(body),
    };
  }

  ast::DataTypeDefinition parseDataTypeDefinition() {
    const auto& position = it_->position;
    expect<tokenization::Data>();
    auto name = expectName<tokenization::UpperVariable>();
    expect<tokenization::Equal>();
    expect<tokenization::LeftBrace>();

    std::vector<ast::Constructor> constructors{parseConstructor()};
    while (match<tokenization::Comma>()) {
      constructors.push_back(parseConstructor());
    }

    expect<tokenization::RightBrace>();

    return {
        allocateNode(position),
        std::move(name),
        std::move(constructors),
    };
  }

  ast::Constructor parseConstructor() {
    const auto& position = it_->position;
    auto name = expectName<tokenization::UpperVariable>();
    auto fields = parseNamesWhile<tokenization::UpperVariable>();

    return {
        allocateNode(position),
        std::move(name),
        std::move(fields),
    };
  }

  // --- Expressions ---

  template <typename ParseOperand, typename MatchOperator>
  ast::ExpressionPtr parseBinaryLevel(ParseOperand parseOperand,
                                      MatchOperator matchOperator) {
    auto left = parseOperand();

    while (!atEnd()) {
      const auto& position = it_->position;
      const auto factory = matchOperator();
      if (!factory) {
        break;
      }

      auto node_id = allocateNode(position);
      auto right = parseOperand();
      left = (*factory)(node_id, std::move(left), std::move(right));
    }

    return left;
  }

  template <typename BinOp>
  static ast::Expression makeBinaryOperator(ast::NodeId id,
                                            ast::ExpressionPtr left,
                                            ast::ExpressionPtr right) {
    return BinOp{
        id,
        std::move(left),
        std::move(right),
    };
  }

  template <typename Token, typename AstNode, typename... Rest>
  std::optional<BinaryOperatorFactory> matchBinaryOperator() {
    if (match<Token>()) {
      return &makeBinaryOperator<AstNode>;
    }
    if constexpr (sizeof...(Rest) > 0) {
      return matchBinaryOperator<Rest...>();
    }
    return std::nullopt;
  }

  // Expression = Term { ("+" | "-") Term }
  ast::ExpressionPtr parseExpression() {
    return parseBinaryLevel(
        [this] { return parseTerm(); },
        [this] {
          return matchBinaryOperator<tokenization::Plus, ast::Addition,
                                     tokenization::Minus, ast::Subtraction>();
        });
  }

  // Term = Factor { ("*" | "/") Factor }
  ast::ExpressionPtr parseTerm() {
    return parseBinaryLevel(
        [this] { return parseFactor(); },
        [this] {
          return matchBinaryOperator<tokenization::Star, ast::Multiplication,
                                     tokenization::Slash, ast::Division>();
        });
  }

  // Factor = Application
  ast::ExpressionPtr parseFactor() { return parseApplication(); }

  // Application = Primary { Primary }
  ast::ExpressionPtr parseApplication() {
    auto expression = parsePrimary();

    while (checkAny<tokenization::IntLiteral, tokenization::LowerVariable,
                    tokenization::UpperVariable, tokenization::LeftParentheses,
                    tokenization::Case>()) {
      const auto& position = it_->position;
      auto argument = parsePrimary();
      expression = ast::Application{
          allocateNode(position),
          std::move(expression),
          std::move(argument),
      };
    }

    return expression;
  }

  // Primary = num | lowerVar | upperVar | "(" Expression ")" | CaseExpression
  ast::ExpressionPtr parsePrimary() {
    if (check<tokenization::LeftParentheses>()) {
      return parseParenthesized();
    }
    if (check<tokenization::IntLiteral>()) {
      return parseIntLiteral();
    }
    if (checkAny<tokenization::LowerVariable, tokenization::UpperVariable>()) {
      return parseVariable();
    }
    if (check<tokenization::Case>()) {
      return parseCaseExpression();
    }
    throwExpected("expression");
  }

  ast::ExpressionPtr parseIntLiteral() {
    const auto& position = it_->position;
    return {ast::IntLiteral{
        allocateNode(position),
        expect<tokenization::IntLiteral>().value,
    }};
  }

  ast::ExpressionPtr parseVariable() {
    const auto& position = it_->position;
    auto name = expectAnyName<tokenization::LowerVariable,
                              tokenization::UpperVariable>();
    return {ast::Variable{
        allocateNode(position),
        std::move(name),
    }};
  }

  ast::ExpressionPtr parseParenthesized() {
    expect<tokenization::LeftParentheses>();
    auto expression = parseExpression();
    expect<tokenization::RightParentheses>();
    return expression;
  }

  // CaseExpression = "case" Expression "of" "{" Branch { Branch } "}"
  ast::ExpressionPtr parseCaseExpression() {
    const auto& position = it_->position;
    expect<tokenization::Case>();
    auto scrutinee = parseExpression();
    expect<tokenization::Of>();
    expect<tokenization::LeftBrace>();

    std::vector<ast::Branch> branches{parseBranch()};
    while (!check<tokenization::RightBrace>() && !atEnd()) {
      branches.push_back(parseBranch());
    }

    expect<tokenization::RightBrace>();

    return {ast::CaseExpression{
        allocateNode(position),
        std::move(scrutinee),
        std::move(branches),
    }};
  }

  // Branch = Pattern "->" "{" Expression "}"
  ast::Branch parseBranch() {
    const auto& position = it_->position;
    auto pattern = parsePattern();
    expect<tokenization::Arrow>();
    expect<tokenization::LeftBrace>();
    auto body = parseExpression();
    expect<tokenization::RightBrace>();

    return {
        allocateNode(position),
        std::move(pattern),
        std::move(body),
    };
  }

  // Pattern = lowerVar | upperVar { lowerVar }
  ast::Pattern parsePattern() {
    if (check<tokenization::LowerVariable>()) {
      return parseVariablePattern();
    }
    if (check<tokenization::UpperVariable>()) {
      return parseConstructorPattern();
    }
    throwExpected("pattern");
  }

  ast::Pattern parseVariablePattern() {
    const auto& position = it_->position;
    return {ast::VariablePattern{
        allocateNode(position),
        expectName<tokenization::LowerVariable>(),
    }};
  }

  ast::Pattern parseConstructorPattern() {
    const auto& position = it_->position;
    return {ast::ConstructorPattern{
        allocateNode(position),
        expectName<tokenization::UpperVariable>(),
        parseNamesWhile<tokenization::LowerVariable>(),
    }};
  }
};

}  // namespace

ParsedProgram parse(const std::vector<tokenization::Token>& tokens) {
  return Parser{tokens}.parse();
}

}  // namespace parsing
