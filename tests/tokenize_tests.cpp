#include <gtest/gtest.h>
#include <vector>

#include "tokenization/tokenize.hpp"

namespace {

TEST(Tokenize, ReturnsEmptySequenceForBlankInput) {
  EXPECT_TRUE(tokenization::Tokenize("   \n\t  ").empty());
}

TEST(Tokenize, SplitsWhitespaceSeparatedWords) {
  const std::vector<std::string> expected{"let", "answer", "42"};
  EXPECT_EQ(tokenization::Tokenize("let answer 42"), expected);
}

}  // namespace
