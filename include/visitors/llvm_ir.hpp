#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace parsing {
struct ParsedProgram;
}  // namespace parsing

namespace visitors {

struct LlvmIrOptions {
  std::string module_name = "mf_module";
  std::filesystem::path output_path = "bin/mf.ll";
  std::string target_triple;
  std::string data_layout;
};

enum class LlvmOptimizationLevel : std::uint8_t {
  O0,
  O1,
  O2,
  O3,
};

struct LlvmObjectOptions {
  std::string module_name = "mf_module";
  std::filesystem::path output_path = "bin/mf.o";
  std::string target_triple;
  LlvmOptimizationLevel optimization = LlvmOptimizationLevel::O0;
};

[[nodiscard]] std::string generateLlvmIr(
    const parsing::ParsedProgram& parsed,
    const LlvmIrOptions& options = LlvmIrOptions{});

[[nodiscard]] std::filesystem::path writeLlvmIr(
    const parsing::ParsedProgram& parsed,
    const LlvmIrOptions& options = LlvmIrOptions{});

[[nodiscard]] std::string defaultTargetTriple();

[[nodiscard]] std::filesystem::path writeObjectFile(
    const parsing::ParsedProgram& parsed,
    const LlvmObjectOptions& options = LlvmObjectOptions{});

}  // namespace visitors
