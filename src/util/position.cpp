#include "util/position.hpp"

#include <format>

namespace util {

std::string Position::toString() const {
  return std::format("Line {}, Col {} : Line {}, Col {}", begin_line,
                     begin_column, end_line, end_column);
}

}  // namespace util
