#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tokenization {

std::vector<std::string> Tokenize(std::string_view source);

}  // namespace tokenization
