#include <gtest/gtest.h>
#include <vector>

#include "parsing/parse.hpp"

namespace {

TEST(Parse, RejectsEmptyTokenStream) {
  const parsing::ParseResult result = parsing::Parse({});

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.token_count, 0U);
  EXPECT_EQ(result.root, "empty");
}

TEST(Parse, ProducesProgramRootForNonEmptyInput) {
  const std::vector<std::string> tokens{"let", "answer", "42"};
  const parsing::ParseResult result = parsing::Parse(tokens);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.token_count, tokens.size());
  EXPECT_EQ(result.root, "program");
}

}  // namespace
