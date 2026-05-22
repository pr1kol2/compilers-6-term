#pragma once

namespace util {

template <class... Ts>
// NOLINTNEXTLINE (readability-identifier-naming)
struct overloaded : Ts... {
  using Ts::operator()...;
};

}  // namespace util
