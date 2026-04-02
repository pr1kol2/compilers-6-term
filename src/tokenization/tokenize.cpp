#include "tokenization/tokenize.hpp"

#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <string_view>

#include "tokenization/token.hpp"
#include "util/position.hpp"

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
  explicit Scanner(std::string_view source)
      : it_(source.begin()), end_(source.cend()) {}

  std::vector<Token> scan() {
    while (!atEnd()) {
      if (isSpace(*it_)) {
        advance();
        continue;
      }
      scanToken();
    }
    return std::move(tokens_);
  }

 private:
  using SVIterator = std::string_view::const_iterator;

  std::vector<Token> tokens_;
  SVIterator it_;
  const SVIterator end_;  // NOLINT
  util::Position pos_ = {
      .begin_line = 1, .begin_column = 1, .end_line = 1, .end_column = 1};

  [[nodiscard]] bool atEnd() const { return it_ == end_; }

  char advance() {
    char character = *it_;
    std::advance(it_, 1);
    if (character == '\n') {
      ++pos_.end_line;
      pos_.end_column = 1;
    } else {
      ++pos_.end_column;
    }
    return character;
  }

  template <typename V>
  void emit(V&& load) {
    tokens_.emplace_back(Token{std::forward<V>(load), pos_});
  }

  void scanToken() {
    pos_.begin_line = pos_.end_line;
    pos_.begin_column = pos_.end_column;
    char character = *it_;

    if (isAlpha(character)) {
      scanWord();
    } else if (isDigit(character)) {
      scanNumber();
    } else if (character == '-' && std::next(it_) != end_ &&
               *std::next(it_) == '>') {
      scanArrow();
    } else if (auto token = singleCharToken(character)) {
      advance();
      emit(std::move(*token));
    } else {
      throw std::runtime_error(std::format("Unexpected character '{}' at {}",
                                           character, pos_.toString()));
    }
  }

  void scanWord() {
    bool is_lower = isLower(*it_);
    const auto* const start = it_;
    while (!atEnd() && isAlpha(*it_)) {
      advance();
    }
    std::string_view word{start, it_};

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
    while (!atEnd() && isDigit(*it_)) {
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
