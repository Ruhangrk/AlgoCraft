#pragma once

#include <string>

#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/ids.hpp"

namespace algocraft {

struct User {
  UserId id{};
  std::string username;
  Role role{Role::User};
};

}  // namespace algocraft
