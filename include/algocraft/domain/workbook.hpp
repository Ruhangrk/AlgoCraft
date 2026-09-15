#pragma once

#include <string>

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/ids.hpp"

namespace algocraft {

struct Workbook {
  WorkbookId id{};
  UserId user_id{};
  std::string name;
  Capital main_capital{};
  WorkbookStatus status{WorkbookStatus::Active};
};

}  // namespace algocraft
