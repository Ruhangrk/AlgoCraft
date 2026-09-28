#include "algocraft/strategies/strategy_compiler.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "algocraft/log/log.hpp"

namespace algocraft {
namespace {

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot read: " + path.string());
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("cannot write: " + path.string());
  }
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

std::string run_cmd(const std::string& cmd) {
  std::array<char, 512> buf{};
  std::string out;
  FILE* pipe = ::popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    return "popen failed for: " + cmd;
  }
  while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    out += buf.data();
  }
  const int rc = ::pclose(pipe);
  if (rc != 0) {
    out += "\n[exit_code=" + std::to_string(rc) + "]\n";
  }
  return out;
}

bool contains_token(std::string_view hay, std::string_view needle) {
  return hay.find(needle) != std::string_view::npos;
}

void ensure_cmake_entry(const std::filesystem::path& cmake, std::string_view cpp_rel) {
  auto text = read_file(cmake);
  const std::string entry = std::string("  ") + std::string(cpp_rel);
  if (contains_token(text, entry) || contains_token(text, std::string(cpp_rel))) {
    return;
  }
  // Insert before strategy_registrations.cpp line (stable anchor).
  const std::string anchor = "  src/strategies/strategy_registrations.cpp";
  const auto pos = text.find(anchor);
  if (pos == std::string::npos) {
    throw std::runtime_error("CMakeLists.txt missing strategy_registrations.cpp anchor");
  }
  text.insert(pos, entry + "\n");
  write_file(cmake, text);
}

void ensure_registration(const std::filesystem::path& reg_cpp, std::string_view name,
                         std::string_view class_name) {
  auto text = read_file(reg_cpp);
  const std::string include_line =
      std::string("#include \"algocraft/strategies/") + std::string(name) + ".hpp\"";
  const std::string add_line = std::string("  registry.add(\"") + std::string(name) +
                               "\", [] { return std::make_unique<" + std::string(class_name) +
                               ">(); });";

  if (!contains_token(text, include_line)) {
    const std::string include_anchor = "#include \"algocraft/strategies/vwap_reversion.hpp\"";
    auto pos = text.find(include_anchor);
    if (pos == std::string::npos) {
      pos = text.find("#include <stdexcept>");
    }
    if (pos == std::string::npos) {
      throw std::runtime_error("strategy_registrations.cpp: cannot find include insert point");
    }
    text.insert(pos, include_line + "\n");
  }

  if (!contains_token(text, std::string("registry.add(\"") + std::string(name) + "\"")) {
    const std::string add_anchor = "  registry.add(\"nr7_breakout\"";
    auto pos = text.find(add_anchor);
    if (pos == std::string::npos) {
      // Fall back: before closing brace of register_all_strategies.
      pos = text.rfind("}\n\n}  // namespace algocraft");
      if (pos == std::string::npos) {
        throw std::runtime_error("strategy_registrations.cpp: cannot find add insert point");
      }
      text.insert(pos, add_line + "\n");
    } else {
      // Insert after the nr7_breakout line.
      const auto eol = text.find('\n', pos);
      text.insert(eol == std::string::npos ? text.size() : eol + 1, add_line + "\n");
    }
  }

  write_file(reg_cpp, text);
}

}  // namespace

StrategyCompiler::StrategyCompiler(Paths paths) : paths_{std::move(paths)} {
  if (paths_.source_root.empty()) {
    throw std::invalid_argument("StrategyCompiler: source_root required");
  }
  if (paths_.sandbox_root.empty()) {
    paths_.sandbox_root = paths_.source_root / "data" / "agent_sandbox";
  }
}

bool StrategyCompiler::valid_name(std::string_view name) {
  if (name.empty() || name.size() > 64) {
    return false;
  }
  if (!std::islower(static_cast<unsigned char>(name[0]))) {
    return false;
  }
  for (char c : name) {
    const auto u = static_cast<unsigned char>(c);
    if (!(std::islower(u) || std::isdigit(u) || c == '_')) {
      return false;
    }
  }
  return true;
}

std::string StrategyCompiler::derive_class_name(std::string_view snake) {
  std::string out;
  bool cap = true;
  for (char c : snake) {
    if (c == '_') {
      cap = true;
      continue;
    }
    if (cap) {
      out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
      cap = false;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

StrategyCompiler::Result StrategyCompiler::compile(const Request& req) const {
  Result result;
  if (!valid_name(req.name)) {
    result.log = "invalid name: use snake_case [a-z][a-z0-9_]{0,63}";
    return result;
  }
  if (req.hpp.empty() || req.cpp.empty()) {
    result.log = "hpp and cpp source required";
    return result;
  }

  result.class_name = req.class_name.empty() ? derive_class_name(req.name) : req.class_name;
  result.sandbox_dir = paths_.sandbox_root / req.name;
  const auto hpp_abs =
      result.sandbox_dir / "include" / "algocraft" / "strategies" / (req.name + ".hpp");
  const auto cpp_abs = result.sandbox_dir / "src" / (req.name + ".cpp");
  result.hpp_path = std::filesystem::path("include") / "algocraft" / "strategies" / (req.name + ".hpp");
  result.cpp_path = std::filesystem::path("src") / "strategies" / (req.name + ".cpp");

  try {
    if (std::filesystem::exists(result.sandbox_dir)) {
      std::filesystem::remove_all(result.sandbox_dir);
    }
    write_file(hpp_abs, req.hpp);
    write_file(cpp_abs, req.cpp);
    write_file(result.sandbox_dir / "class_name.txt", result.class_name);

    const auto project_include = paths_.source_root / "include";
    const auto sandbox_include = result.sandbox_dir / "include";
    // Syntax-only: validates against live project headers + sandbox override for the new hpp.
    std::ostringstream cmd;
    cmd << "g++ -std=c++20 -fsyntax-only -Wall -Wextra"
        << " -I" << sandbox_include.string()
        << " -I" << project_include.string()
        << " " << cpp_abs.string() << " 2>&1";

    result.log = run_cmd(cmd.str());
    const bool failed =
        result.log.find("[exit_code=") != std::string::npos ||
        result.log.find("error:") != std::string::npos;
    result.ok = !failed;
    write_file(result.sandbox_dir / "compile.log", result.log);
    AC_LOG_INFO("agent_compile name={} ok={}", req.name, result.ok);
  } catch (const std::exception& e) {
    result.ok = false;
    result.log = e.what();
  }
  return result;
}

StrategyCompiler::PromoteResult StrategyCompiler::promote(std::string_view name) const {
  PromoteResult result;
  if (!valid_name(name)) {
    result.log = "invalid name";
    return result;
  }

  const auto sandbox = paths_.sandbox_root / std::string(name);
  const auto hpp_src =
      sandbox / "include" / "algocraft" / "strategies" / (std::string(name) + ".hpp");
  const auto cpp_src = sandbox / "src" / (std::string(name) + ".cpp");
  if (!std::filesystem::exists(hpp_src) || !std::filesystem::exists(cpp_src)) {
    result.log = "sandbox sources missing; call /agent/strategies/compile first";
    return result;
  }

  try {
    result.class_name = derive_class_name(name);
    // Prefer class name from a prior successful compile marker file if present.
    const auto meta = sandbox / "class_name.txt";
    if (std::filesystem::exists(meta)) {
      result.class_name = read_file(meta);
      while (!result.class_name.empty() &&
             (result.class_name.back() == '\n' || result.class_name.back() == '\r')) {
        result.class_name.pop_back();
      }
    }

    result.hpp_path = std::filesystem::path("include") / "algocraft" / "strategies" /
                      (std::string(name) + ".hpp");
    result.cpp_path = std::filesystem::path("src") / "strategies" / (std::string(name) + ".cpp");

    const auto hpp_dst = paths_.source_root / result.hpp_path;
    const auto cpp_dst = paths_.source_root / result.cpp_path;
    write_file(hpp_dst, read_file(hpp_src));
    write_file(cpp_dst, read_file(cpp_src));

    ensure_cmake_entry(paths_.source_root / "CMakeLists.txt", result.cpp_path.string());
    ensure_registration(paths_.source_root / "src" / "strategies" / "strategy_registrations.cpp",
                        name, result.class_name);

    result.ok = true;
    result.log = "promoted to " + result.hpp_path.string() + " and " + result.cpp_path.string() +
                 "; CMake + strategy_registrations updated; catalog should stay enabled=0 until activate";
    AC_LOG_INFO("agent_promote name={} class={}", name, result.class_name);
  } catch (const std::exception& e) {
    result.ok = false;
    result.log = e.what();
  }
  return result;
}

}  // namespace algocraft
