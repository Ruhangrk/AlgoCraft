#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace algocraft {

// Sandbox write + g++ -fsyntax-only check for agent-generated strategies.
class StrategyCompiler {
public:
  struct Paths {
    std::filesystem::path source_root;     // repo root (has include/, src/, CMakeLists.txt)
    std::filesystem::path sandbox_root;    // e.g. data/agent_sandbox
  };

  struct Request {
    std::string name;         // snake_case, e.g. my_mean_revert
    std::string class_name;   // optional; derived from name if empty
    std::string hpp;
    std::string cpp;
    std::string kind{"strategy"};
  };

  struct Result {
    bool ok{false};
    std::string log;
    std::string class_name;
    std::filesystem::path sandbox_dir;
    std::filesystem::path hpp_path;  // relative to source_root after promote layout in sandbox
    std::filesystem::path cpp_path;
  };

  explicit StrategyCompiler(Paths paths);

  [[nodiscard]] static bool valid_name(std::string_view name);
  [[nodiscard]] static std::string derive_class_name(std::string_view snake);

  // Writes sandbox tree and runs syntax-only compile against project headers.
  [[nodiscard]] Result compile(const Request& req) const;

  // Copies sandbox sources into include/ + src/, patches CMake + registrations.
  struct PromoteResult {
    bool ok{false};
    std::string log;
    std::string class_name;
    std::filesystem::path hpp_path;  // relative to source_root
    std::filesystem::path cpp_path;
  };
  [[nodiscard]] PromoteResult promote(std::string_view name) const;

private:
  Paths paths_{};
};

}  // namespace algocraft
