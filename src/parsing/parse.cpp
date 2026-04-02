#include "parsing/parse.hpp"

#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/position.hpp"

namespace parsing {

namespace {

template <typename Node>
ast::Expression makeExpression(Node node) {
  return ast::Expression{std::move(node)};
}

class Parser {
 public:
  explicit Parser(std::span<const tokenization::Token> tokens)
      : tokens_(tokens) {}

  ParsedProgram parseProgram() {
    ast::Program program;
    while (!atEnd()) {
      program.definitions.push_back(parseDefinition());
    }

    return {
        .ast = std::move(program),
        .positions = std::move(positions_),
    };
  }

 private:
  using BinaryFactory = ast::Expression (*)(ast::NodeId, ast::ExpressionPtr,
                                            ast::ExpressionPtr);

  std::span<const tokenization::Token> tokens_;
  std::vector<util::Position> positions_;
  std::size_t cursor_ = 0;

  // --- Token helpers ---

  [[nodiscard]] bool atEnd() const { return cursor_ >= tokens_.size(); }

  [[nodiscard]] const tokenization::Token& current() const {
    return tokens_[cursor_];
  }

  [[nodiscard]] const util::Position& currentPosition() const {
    return current().position;
  }

  [[nodiscard]] ast::NodeId allocateNode(util::Position position) {
    if (positions_.size() >= ast::kInvalidNodeId) {
      throw std::overflow_error("Too many AST nodes");
    }

    const auto node_id = positions_.size();
    positions_.push_back(position);
    return node_id;
  }

  const tokenization::Token& advance() { return tokens_[cursor_++]; }

  template <typename T>
  [[nodiscard]] bool check() const {
    return !atEnd() && std::holds_alternative<T>(current().load);
  }

  template <typename... T>
  [[nodiscard]] bool checkAny() const {
    return (check<T>() || ...);
  }

  template <typename T>
  const T& expect() {
    if (!check<T>()) {
      if (atEnd()) {
        throw std::runtime_error("Unexpected end of input");
      }
      throw std::runtime_error(
          std::format("Unexpected token at {}", currentPosition().toString()));
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
    throw std::runtime_error(std::format("Expected {} at {}", expected,
                                         currentPosition().toString()));
  }

  template <typename Token>
  std::string expectName() {
    return std::string(expect<Token>().name);
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
    const auto position = currentPosition();
    expect<tokenization::Function>();
    auto name = expectName<tokenization::LowerVariable>();
    auto parameters = parseNamesWhile<tokenization::LowerVariable>();

    expect<tokenization::Equal>();
    expect<tokenization::LeftBrace>();
    auto body = parseExpression();
    expect<tokenization::RightBrace>();

    return {
        .id = allocateNode(position),
        .name = std::move(name),
        .parameters = std::move(parameters),
        .body = std::move(body),
    };
  }

  ast::DataTypeDefinition parseDataTypeDefinition() {
    const auto position = currentPosition();
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
        .id = allocateNode(position),
        .name = std::move(name),
        .constructors = std::move(constructors),
    };
  }

  ast::Constructor parseConstructor() {
    const auto position = currentPosition();
    auto name = expectName<tokenization::UpperVariable>();
    auto fields = parseNamesWhile<tokenization::UpperVariable>();

    return {
        .id = allocateNode(position),
        .name = std::move(name),
        .fields = std::move(fields),
    };
  }

  // --- Expressions (precedence climbing) ---

  template <typename ParseOperand, typename MatchOperator>
  ast::ExpressionPtr parseBinaryLevel(ParseOperand parseOperand,
                                      MatchOperator matchOperator) {
    auto left = parseOperand();

    while (!atEnd()) {
      const auto position = currentPosition();
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
  static ast::Expression makeBinaryOp(ast::NodeId id, ast::ExpressionPtr left,
                                      ast::ExpressionPtr right) {
    return makeExpression(BinOp{
        .id = id,
        .left_operand = std::move(left),
        .right_operand = std::move(right),
    });
  }

  // Expression = Term { ("+" | "-") Term }
  ast::ExpressionPtr parseExpression() {
    return parseBinaryLevel([this] { return parseTerm(); },
                            [this]() -> std::optional<BinaryFactory> {
                              if (match<tokenization::Plus>()) {
                                return &makeBinaryOp<ast::Addition>;
                              }
                              if (match<tokenization::Minus>()) {
                                return &makeBinaryOp<ast::Subtraction>;
                              }
                              return std::nullopt;
                            });
  }

  // Term = Factor { ("*" | "/") Factor }
  ast::ExpressionPtr parseTerm() {
    return parseBinaryLevel([this] { return parseFactor(); },
                            [this]() -> std::optional<BinaryFactory> {
                              if (match<tokenization::Star>()) {
                                return &makeBinaryOp<ast::Multiplication>;
                              }
                              if (match<tokenization::Slash>()) {
                                return &makeBinaryOp<ast::Division>;
                              }
                              return std::nullopt;
                            });
  }

  // Factor = Application
  ast::ExpressionPtr parseFactor() { return parseApplication(); }

  // Application = Primary { Primary }
  ast::ExpressionPtr parseApplication() {
    auto expression = parsePrimary();

    while (canStartPrimary()) {
      const auto position = currentPosition();
      auto argument = parsePrimary();
      expression = makeExpression(ast::Application{
          .id = allocateNode(position),
          .function = std::move(expression),
          .argument = std::move(argument),
      });
    }

    return expression;
  }

  [[nodiscard]] bool canStartPrimary() const {
    return checkAny<tokenization::IntLiteral, tokenization::LowerVariable,
                    tokenization::UpperVariable, tokenization::LeftParentheses,
                    tokenization::Case>();
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
    const auto position = currentPosition();
    return makeExpression(ast::IntLiteral{
        .id = allocateNode(position),
        .value = expect<tokenization::IntLiteral>().value,
    });
  }

  ast::ExpressionPtr parseVariable() {
    const auto position = currentPosition();
    auto name = check<tokenization::LowerVariable>()
                    ? expectName<tokenization::LowerVariable>()
                    : expectName<tokenization::UpperVariable>();
    return makeExpression(ast::Variable{
        .id = allocateNode(position),
        .name = std::move(name),
    });
  }

  ast::ExpressionPtr parseParenthesized() {
    expect<tokenization::LeftParentheses>();
    auto expression = parseExpression();
    expect<tokenization::RightParentheses>();
    return expression;
  }

  // CaseExpression = "case" Expression "of" "{" Branch { Branch } "}"
  ast::ExpressionPtr parseCaseExpression() {
    const auto position = currentPosition();
    expect<tokenization::Case>();
    auto scrutinee = parseExpression();
    expect<tokenization::Of>();
    expect<tokenization::LeftBrace>();

    std::vector<ast::Branch> branches{parseBranch()};
    while (!check<tokenization::RightBrace>() && !atEnd()) {
      branches.push_back(parseBranch());
    }

    expect<tokenization::RightBrace>();

    return makeExpression(ast::CaseExpression{
        .id = allocateNode(position),
        .scrutinee = std::move(scrutinee),
        .branches = std::move(branches),
    });
  }

  // Branch = Pattern "->" "{" Expression "}"
  ast::Branch parseBranch() {
    const auto position = currentPosition();
    auto pattern = parsePattern();
    expect<tokenization::Arrow>();
    expect<tokenization::LeftBrace>();
    auto body = parseExpression();
    expect<tokenization::RightBrace>();

    return {
        .id = allocateNode(position),
        .pattern = std::move(pattern),
        .body = std::move(body),
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
    const auto position = currentPosition();
    return ast::Pattern{ast::VariablePattern{
        .id = allocateNode(position),
        .name = expectName<tokenization::LowerVariable>(),
    }};
  }

  ast::Pattern parseConstructorPattern() {
    const auto position = currentPosition();
    return ast::Pattern{ast::ConstructorPattern{
        .id = allocateNode(position),
        .name = expectName<tokenization::UpperVariable>(),
        .arguments = parseNamesWhile<tokenization::LowerVariable>(),
    }};
  }
};

}  // namespace

ParsedProgram parse(const std::vector<tokenization::Token>& tokens) {
  return Parser{tokens}.parseProgram();
}

}  // namespace parsing
