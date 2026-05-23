#include <filesystem>
#include <format>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "parsing/parse.hpp"
#include "tokenization/tokenize.hpp"
#include "visitors/llvm_ir.hpp"

#ifndef MF_CLANG_PATH
#define MF_CLANG_PATH "clang"
#endif

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,
// bugprone-throwing-static-initialization)

llvm::cl::opt<std::string> input_path{
    llvm::cl::Positional,
    llvm::cl::desc("<input.mf>"),
    llvm::cl::Required,
};

llvm::cl::opt<std::string> llvm_output_path{
    "llvm-output",
    llvm::cl::desc("LLVM IR output path"),
    llvm::cl::value_desc("path"),
    llvm::cl::init("bin/mf.ll"),
};

llvm::cl::opt<std::string> object_output_path{
    "object-output",
    llvm::cl::desc("Object file output path"),
    llvm::cl::value_desc("path"),
    llvm::cl::init("bin/mf.o"),
};

llvm::cl::opt<std::string> executable_output_path{
    "o",
    llvm::cl::desc("Executable output path"),
    llvm::cl::value_desc("path"),
    llvm::cl::init("bin/mf_program"),
};

llvm::cl::alias executable_output_alias{
    "output",
    llvm::cl::desc("Alias for -o"),
    llvm::cl::aliasopt(executable_output_path),
};

llvm::cl::opt<std::string> module_name{
    "module-name",
    llvm::cl::desc("LLVM module name"),
    llvm::cl::value_desc("name"),
    llvm::cl::init("mf_module"),
};

llvm::cl::opt<std::string> target_triple{
    "target",
    llvm::cl::desc("Target triple; defaults to current system target"),
    llvm::cl::value_desc("triple"),
    llvm::cl::init(""),
};

llvm::cl::opt<unsigned> optimization_level{
    "opt-level",
    llvm::cl::desc("LLVM optimization level: 0, 1, 2 or 3"),
    llvm::cl::value_desc("level"),
    llvm::cl::init(0),
};

llvm::cl::alias optimization_level_alias{
    "O",
    llvm::cl::desc("Alias for --opt-level"),
    llvm::cl::aliasopt(optimization_level),
};

llvm::cl::opt<bool> emit_llvm{
    "emit-llvm",
    llvm::cl::desc("Write LLVM IR alongside the executable"),
    llvm::cl::init(false),
};

llvm::cl::opt<bool> emit_object{
    "emit-obj",
    llvm::cl::desc("Keep generated object file"),
    llvm::cl::init(false),
};

llvm::cl::opt<std::string> linker_path{
    "linker",
    llvm::cl::desc("Linker driver used to build the executable"),
    llvm::cl::value_desc("path"),
    llvm::cl::init(MF_CLANG_PATH),
};

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,
// bugprone-throwing-static-initialization)

[[nodiscard]] parsing::ParsedProgram parseFile(std::string_view path) {
  auto buffer = llvm::MemoryBuffer::getFile(path);
  if (!buffer) {
    throw std::runtime_error(
        std::format("Cannot read '{}': {}", path, buffer.getError().message()));
  }

  const auto source = (*buffer)->getBuffer();
  return parsing::parse(
      tokenization::tokenize(std::string_view{source.data(), source.size()}));
}

[[nodiscard]] visitors::LlvmOptimizationLevel parseOptimizationLevel(
    unsigned level) {
  switch (level) {
    case 0:
      return visitors::LlvmOptimizationLevel::O0;
    case 1:
      return visitors::LlvmOptimizationLevel::O1;
    case 2:
      return visitors::LlvmOptimizationLevel::O2;
    case 3:
      return visitors::LlvmOptimizationLevel::O3;
    default:
      throw std::runtime_error(
          std::format("Unsupported optimization level {}", level));
  }
}

struct LinkOptions {
  std::string_view object_path;
  std::string_view output_path;
  std::string_view target;
};

void linkExecutable(const LinkOptions& options) {
  const auto linker_program =
      llvm::sys::findProgramByName(linker_path.getValue());
  if (!linker_program) {
    throw std::runtime_error(std::format("Cannot find linker '{}': {}",
                                         linker_path.getValue(),
                                         linker_program.getError().message()));
  }

  const auto parent = std::filesystem::path{options.output_path}.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  std::vector<std::string> argument_storage{
      *linker_program,
      std::string{options.object_path},
      "-o",
      std::string{options.output_path},
  };
  if (!options.target.empty()) {
    argument_storage.push_back(std::format("--target={}", options.target));
  }

  std::vector<llvm::StringRef> arguments;
  arguments.reserve(argument_storage.size());
  for (const auto& argument : argument_storage) {
    arguments.emplace_back(argument);
  }

  std::string message;
  bool execution_failed = false;
  const auto exit_code =
      llvm::sys::ExecuteAndWait(*linker_program, arguments, std::nullopt, {}, 0,
                                0, &message, &execution_failed);
  if (execution_failed || exit_code != 0) {
    if (message.empty()) {
      message = std::format("linker exited with code {}", exit_code);
    }
    throw std::runtime_error(message);
  }
}

}  // namespace

int main(int argc, char** argv) {
  llvm::InitLLVM init_llvm{argc, argv};
  llvm::cl::ParseCommandLineOptions(argc, argv, "MF language compiler\n");

  try {
    const auto parsed = parseFile(input_path.getValue());

    const auto target = target_triple.getValue().empty()
                            ? visitors::defaultTargetTriple()
                            : target_triple.getValue();
    const auto optimization = parseOptimizationLevel(optimization_level);

    if (emit_llvm) {
      const auto llvm_path = visitors::writeLlvmIr(
          parsed, visitors::LlvmIrOptions{
                      .module_name = module_name.getValue(),
                      .output_path = llvm_output_path.getValue(),
                      .target_triple = target,
                      .data_layout = "",
                  });
      llvm::outs() << "wrote " << llvm_path.string() << '\n';
    }

    const auto object_path = visitors::writeObjectFile(
        parsed, visitors::LlvmObjectOptions{
                    .module_name = module_name.getValue(),
                    .output_path = object_output_path.getValue(),
                    .target_triple = target,
                    .optimization = optimization,
                });
    if (emit_object) {
      llvm::outs() << "wrote " << object_path.string() << '\n';
    }

    const auto object_path_string = object_path.string();
    linkExecutable(LinkOptions{
        .object_path = object_path_string,
        .output_path = executable_output_path.getValue(),
        .target = target,
    });
    if (!emit_object) {
      std::error_code error;
      std::filesystem::remove(object_path, error);
    }
    llvm::outs() << "wrote " << executable_output_path.getValue() << '\n';
  } catch (const std::exception& exception) {
    llvm::errs() << "error: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}
