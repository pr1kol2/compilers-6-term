#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>
#include <string_view>

#include "test_utils.hpp"
#include "visitors/llvm_ir.hpp"

// NOLINTBEGIN

namespace {

#ifndef MF_TEST_CLANG_PATH
#define MF_TEST_CLANG_PATH "clang"
#endif

#ifndef MF_TEST_MFCC_PATH
#define MF_TEST_MFCC_PATH "bin/mfcc"
#endif

[[nodiscard]] std::filesystem::path testDirectory() {
  const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
  auto path = std::filesystem::current_path() / "build" / "llvm_ir_tests" /
              std::format("{}_{}", info->test_suite_name(), info->name());
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

[[nodiscard]] std::string quote(const std::filesystem::path& path) {
  return std::format("\"{}\"", path.string());
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

class CurrentPathGuard {
 public:
  explicit CurrentPathGuard(const std::filesystem::path& path)
      : old_path_(std::filesystem::current_path()) {
    std::filesystem::current_path(path);
  }

  CurrentPathGuard(const CurrentPathGuard&) = delete;
  CurrentPathGuard& operator=(const CurrentPathGuard&) = delete;
  CurrentPathGuard(CurrentPathGuard&&) = delete;
  CurrentPathGuard& operator=(CurrentPathGuard&&) = delete;

  ~CurrentPathGuard() { std::filesystem::current_path(old_path_); }

 private:
  std::filesystem::path old_path_;
};

[[nodiscard]] std::string compileAndRun(std::string_view source) {
  const auto clang = std::filesystem::path{MF_TEST_CLANG_PATH};
  if (!std::filesystem::exists(clang)) {
    ADD_FAILURE() << "clang is not available at " << clang;
    return {};
  }

  const auto dir = testDirectory();
  const auto ll_path = dir / "program.ll";
  const auto exe_path = dir / "program";
  const auto stdout_path = dir / "stdout.txt";

  const auto parsed = test_utils::parseSource(source);
  static_cast<void>(visitors::writeLlvmIr(parsed, visitors::LlvmIrOptions{
                                                      .module_name = "mf_test",
                                                      .output_path = ll_path,
                                                      .target_triple = "",
                                                      .data_layout = "",
                                                  }));

  const auto compile_command =
      std::format("{} -Wno-override-module {} -o {}", quote(clang),
                  quote(ll_path), quote(exe_path));
  EXPECT_EQ(std::system(compile_command.c_str()), 0);

  const auto run_command =
      std::format("{} > {}", quote(exe_path), quote(stdout_path));
  EXPECT_EQ(std::system(run_command.c_str()), 0);
  return readFile(stdout_path);
}

[[nodiscard]] std::string writeObjectLinkAndRun(std::string_view source) {
  const auto clang = std::filesystem::path{MF_TEST_CLANG_PATH};
  if (!std::filesystem::exists(clang)) {
    ADD_FAILURE() << "clang is not available at " << clang;
    return {};
  }

  const auto dir = testDirectory();
  const auto object_path = dir / "program.o";
  const auto exe_path = dir / "program";
  const auto stdout_path = dir / "stdout.txt";

  const auto parsed = test_utils::parseSource(source);
  static_cast<void>(visitors::writeObjectFile(
      parsed, visitors::LlvmObjectOptions{
                  .module_name = "mf_test",
                  .output_path = object_path,
                  .target_triple = visitors::defaultTargetTriple(),
                  .optimization = visitors::LlvmOptimizationLevel::O2,
              }));

  const auto target = visitors::defaultTargetTriple();
  const auto link_command =
      std::format("{} --target={} {} -o {}", quote(clang), target,
                  quote(object_path), quote(exe_path));
  EXPECT_EQ(std::system(link_command.c_str()), 0);

  const auto run_command =
      std::format("{} > {}", quote(exe_path), quote(stdout_path));
  EXPECT_EQ(std::system(run_command.c_str()), 0);
  return readFile(stdout_path);
}

[[nodiscard]] std::string compileWithCliAndRun(std::string_view source) {
  const auto mfcc = std::filesystem::path{MF_TEST_MFCC_PATH};
  if (!std::filesystem::exists(mfcc)) {
    ADD_FAILURE() << "mfcc is not available at " << mfcc;
    return {};
  }

  const auto dir = testDirectory();
  const auto source_path = dir / "program.mf";
  const auto ll_path = dir / "program.ll";
  const auto object_path = dir / "program.o";
  const auto exe_path = dir / "program";
  const auto stdout_path = dir / "stdout.txt";
  {
    std::ofstream output{source_path};
    output << source;
  }

  const auto compile_command = std::format(
      "{} {} -o {} --object-output {} --emit-llvm "
      "--llvm-output {} --opt-level=2",
      quote(mfcc), quote(source_path), quote(exe_path), quote(object_path),
      quote(ll_path));
  EXPECT_EQ(std::system(compile_command.c_str()), 0);
  EXPECT_TRUE(std::filesystem::exists(exe_path));
  EXPECT_TRUE(std::filesystem::exists(ll_path));
  EXPECT_FALSE(std::filesystem::exists(object_path));

  const auto run_command =
      std::format("{} > {}", quote(exe_path), quote(stdout_path));
  EXPECT_EQ(std::system(run_command.c_str()), 0);
  return readFile(stdout_path);
}

}  // namespace

TEST(LlvmIr, EmitsModuleWithRuntimeDeclarations) {
  const auto parsed = test_utils::parseSource("defn main = { 1 + 2 * 3 }");
  const auto ir = visitors::generateLlvmIr(parsed);

  EXPECT_TRUE(ir.contains("define i64 @mf."));
  EXPECT_TRUE(ir.contains("define i32 @main()"));
  EXPECT_TRUE(ir.contains("declare i32 @printf"));
}

TEST(LlvmIr, EmitsConstructorAllocationAndSwitch) {
  const auto parsed = test_utils::parseSource(
      "data Bool = { True, False } "
      "defn main = { case True of { True -> { 1 } False -> { 0 } } }");
  const auto ir = visitors::generateLlvmIr(parsed);

  EXPECT_TRUE(ir.contains("%mf.node = type { i64, i64, ptr }"));
  EXPECT_TRUE(ir.contains("declare ptr @malloc"));
  EXPECT_TRUE(ir.contains("switch i64"));
}

TEST(LlvmIr, WritesLlvmIrToBinByDefault) {
  const auto parsed = test_utils::parseSource("defn main = { 42 }");
  const auto dir = testDirectory();
  const CurrentPathGuard current_path_guard{dir};

  const auto path = visitors::writeLlvmIr(parsed);

  EXPECT_EQ(path, std::filesystem::path("bin/mf.ll"));
  EXPECT_TRUE(std::filesystem::exists(dir / path));
  EXPECT_TRUE(readFile(dir / path).contains("define i32 @main()"));
}

TEST(LlvmIr, EmitsSystemTargetTriple) {
  const auto parsed = test_utils::parseSource("defn main = { 42 }");
  const auto target = visitors::defaultTargetTriple();
  const auto ir =
      visitors::generateLlvmIr(parsed, visitors::LlvmIrOptions{
                                           .module_name = "mf_test",
                                           .output_path = "bin/mf.ll",
                                           .target_triple = target,
                                           .data_layout = "",
                                       });

  EXPECT_TRUE(ir.contains(std::format("target triple = \"{}\"", target)));
}

TEST(LlvmIr, RejectsInvalidInputBeforeGeneration) {
  const auto parsed =
      test_utils::parseSource("data Bool = { True } defn main = { True + 1 }");

  EXPECT_THROW(static_cast<void>(visitors::generateLlvmIr(parsed)),
               std::runtime_error);
}

TEST(LlvmIr, RunsArithmeticProgram) {
  EXPECT_EQ(compileAndRun("defn main = { 1 + 2 * 3 }"), "7\n");
}

TEST(LlvmIr, RunsFunctionCallProgram) {
  EXPECT_EQ(compileAndRun("defn inc x = { x + 1 } defn main = { inc 41 }"),
            "42\n");
}

TEST(LlvmIr, RunsForwardFunctionCallProgram) {
  EXPECT_EQ(compileAndRun("defn main = { inc 41 } defn inc x = { x + 1 }"),
            "42\n");
}

TEST(LlvmIr, RunsNullaryConstructorProgram) {
  EXPECT_EQ(compileAndRun("data Bool = { True, False } defn main = { False }"),
            "1\n");
}

TEST(LlvmIr, RunsConstructorCaseProgram) {
  EXPECT_EQ(compileAndRun("data Bool = { True, False } "
                          "defn main = { case False of { True -> { 1 } "
                          "False -> { 0 } } }"),
            "0\n");
}

TEST(LlvmIr, RunsVariablePatternProgram) {
  EXPECT_EQ(compileAndRun("defn main = { case 42 of { x -> { x + 1 } } }"),
            "43\n");
}

TEST(LlvmIr, RunsNestedConstructorDestructuringProgram) {
  EXPECT_EQ(compileAndRun("data List = { Nil, Cons Int List } "
                          "defn main = { case Cons 7 Nil of { "
                          "Nil -> { 0 } Cons x xs -> { x } } }"),
            "7\n");
}

TEST(LlvmIr, RunsNativeObjectPipelineProgram) {
  EXPECT_EQ(writeObjectLinkAndRun("data List = { Nil, Cons Int List } "
                                  "defn main = { case Cons 41 Nil of { "
                                  "Nil -> { 0 } Cons x xs -> { x + 1 } } }"),
            "42\n");
}

TEST(LlvmIr, RunsCliCompilerExecutablePipeline) {
  EXPECT_EQ(compileWithCliAndRun("defn inc x = { x + 1 } "
                                 "defn main = { inc 41 }"),
            "42\n");
}

// NOLINTEND
