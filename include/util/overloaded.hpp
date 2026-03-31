#pragma once

namespace util {

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

}  // namespace util
