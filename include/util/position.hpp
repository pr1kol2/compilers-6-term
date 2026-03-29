#pragma once

#include <string>

namespace util {

struct Position {
  std::size_t begin_line;
  std::size_t begin_column;
  std::size_t end_line;
  std::size_t end_column;

  bool operator==(const Position&) const = default;

  std::string toString() const;
};

}  // namespace util
