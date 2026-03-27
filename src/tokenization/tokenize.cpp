#include "tokenization/tokenize.hpp"

#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace tokenization {

namespace {

const std::unordered_map<std::string, TokenVariant> kKeywords = {
    {"defn", Definition{}},
    {"data", Data{}},
    {"case", Case{}},
    {"of", Of{}},
};

std::optional<TokenVariant> singleCharToken(char c) {
  switch (c) {
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
    while (pos_ < source_.size()) {
      skipWhitespace();
      if (pos_ >= source_.size()) {
        break;
      }
      scanToken();
    }
    return std::move(tokens_);
  }

 private:
  std::string_view source_;
  std::vector<Token> tokens_;
  std::size_t pos_ = 0;
  std::size_t line_ = 1;
  std::size_t col_ = 1;

  [[nodiscard]] char peek() const {
    return pos_ < source_.size() ? source_[pos_] : '\0';
  }

  char advance() {
    char c = source_[pos_++];
    if (c == '\n') {
      ++line_;
      col_ = 1;
    } else {
      ++col_;
    }
    return c;
  }

  void emit(TokenVariant data, std::size_t bl, std::size_t bc) {
    tokens_.push_back({.load = std::move(data),
                       .position = {.begin_line = bl,
                                    .begin_column = bc,
                                    .end_line = line_,
                                    .end_column = col_}});
  }

  void skipWhitespace() {
    while (pos_ < source_.size() && std::isspace(peek()) != 0) {
      advance();
    }
  }

  void scanToken() {
    std::size_t bl = line_;
    std::size_t bc = col_;
    char c = peek();

    if (std::isalpha(c) != 0) {
      scanWord(bl, bc);
    } else if (std::isdigit(c) != 0) {
      scanNumber(bl, bc);
    } else if (c == '-' && pos_ + 1 < source_.size() &&
               source_[pos_ + 1] == '>') {
      scanArrow(bl, bc);
    } else if (auto tok = singleCharToken(c)) {
      advance();
      emit(*tok, bl, bc);
    } else {
      throw std::runtime_error("Unexpected character '" + std::string(1, c) +
                               "' at " + std::to_string(bl) + ":" +
                               std::to_string(bc));
    }
  }

  void scanWord(std::size_t bl, std::size_t bc) {
    bool lower = std::islower(peek()) != 0;
    std::string word;
    while (pos_ < source_.size() && std::isalpha(peek()) != 0) {
      word += advance();
    }

    if (auto it = kKeywords.find(word); it != kKeywords.end()) {
      emit(it->second, bl, bc);
    } else if (lower) {
      emit(LowerVariable{std::move(word)}, bl, bc);
    } else {
      emit(UpperVariable{std::move(word)}, bl, bc);
    }
  }

  void scanNumber(std::size_t bl, std::size_t bc) {
    std::string num;
    while (pos_ < source_.size() && std::isdigit(peek()) != 0) {
      num += advance();
    }
    emit(IntLiteral{std::stoi(num)}, bl, bc);
  }

  void scanArrow(std::size_t bl, std::size_t bc) {
    advance();
    advance();
    emit(Arrow{}, bl, bc);
  }
};

}  // namespace

std::vector<Token> tokenize(std::string_view source) {
  return Scanner{source}.scan();
}

}  // namespace tokenization
