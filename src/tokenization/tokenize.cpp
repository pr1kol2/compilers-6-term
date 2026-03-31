#include "tokenization/tokenize.hpp"

#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <string_view>

#include "tokenization/token.hpp"

namespace tokenization {

bool isAlpha(char character) { return std::isalpha(character) == 1; }

bool isDigit(char character) { return std::isdigit(character) == 1; }

bool isLower(char character) { return std::islower(character) == 1; }

bool isSpace(char character) { return std::isspace(character) == 1; }

struct KeywordEntry {
  std::string_view keyword;
  TokenVariant token;
};

const std::array kKeywords = {
    KeywordEntry{"defn", Function{}}, KeywordEntry{"data", Data{}},
    KeywordEntry{"case", Case{}}, KeywordEntry{"of", Of{}}};

std::optional<TokenVariant> singleCharToken(char character) {
  switch (character) {
    case '=':
      return Equal{};
    case '+':
      return Plus{};
    case '-':
      return Minus{};
    case '*':
      return Star{};
    case '/':
      return Slash{};
    case '{':
      return LeftBrace{};
    case '}':
      return RightBrace{};
    case '(':
      return LeftParentheses{};
    case ')':
      return RightParentheses{};
    case ',':
      return Comma{};
    default:
      return std::nullopt;
  }
}

class Scanner {
 public:
  explicit Scanner(std::string_view source) : source_(source) {}

  std::vector<Token> scan() {
    while (position_ < source_.size()) {
      skipWhitespace();
      if (position_ >= source_.size()) {
        break;
      }
      scanToken();
    }
    return std::move(tokens_);
  }

 private:
  std::string_view source_;
  std::vector<Token> tokens_;
  std::size_t position_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
  std::size_t token_start_line_ = 1;
  std::size_t token_start_column_ = 1;

  [[nodiscard]] char peek() const {
    return position_ < source_.size() ? source_[position_] : '\0';
  }

  char advance() {
    char character = source_[position_++];
    if (character == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
    return character;
  }

  template <typename V>
  void emit(V&& load) {
    tokens_.emplace_back(Token{.load = std::forward<V>(load),
                               .position = {.begin_line = token_start_line_,
                                            .begin_column = token_start_column_,
                                            .end_line = line_,
                                            .end_column = column_}});
  }

  void skipWhitespace() {
    while (position_ < source_.size() && isSpace(peek())) {
      advance();
    }
  }

  void scanToken() {
    token_start_line_ = line_;
    token_start_column_ = column_;
    char character = peek();

    if (isAlpha(character)) {
      scanWord();
    } else if (isDigit(character)) {
      scanNumber();
    } else if (character == '-' && position_ + 1 < source_.size() &&
               source_[position_ + 1] == '>') {
      scanArrow();
    } else if (auto token = singleCharToken(character)) {
      advance();
      emit(std::move(*token));
    } else {
      util::Position pos{.begin_line = line_,
                         .begin_column = column_,
                         .end_line = line_,
                         .end_column = column_};
      throw std::runtime_error(std::format("Unexpected character '{}' at {}",
                                           character, pos.toString()));
    }
  }

  void scanWord() {
    bool is_lower = isLower(peek());
    std::size_t start = position_;
    while (position_ < source_.size() && isAlpha(peek())) {
      advance();
    }
    std::string_view word = source_.substr(start, position_ - start);

    for (const auto& [text, token] : kKeywords) {
      if (word == text) {
        emit(token);
        return;
      }
    }

    if (is_lower) {
      emit(LowerVariable{std::string(word)});
    } else {
      emit(UpperVariable{std::string(word)});
    }
  }

  void scanNumber() {
    int value = 0;
    while (position_ < source_.size() && isDigit(peek())) {
      const int kDecimalBase = 10;
      value = (value * kDecimalBase) + (advance() - '0');
    }
    emit(IntLiteral{value});
  }

  void scanArrow() {
    advance();
    advance();
    emit(Arrow{});
  }
};

std::vector<Token> tokenize(std::string_view source) {
  return Scanner{source}.scan();
}

}  // namespace tokenization
