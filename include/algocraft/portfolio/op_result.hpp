#pragma once

namespace algocraft {

struct OpResult {
  bool ok{true};
  const char* error{"ok"};

  static OpResult success() { return {}; }
  static OpResult fail(const char* message) { return {false, message}; }
};

}  // namespace algocraft
