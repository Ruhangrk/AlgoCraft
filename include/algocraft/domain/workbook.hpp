#pragma once

#include <string>

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

struct WorkbookCapital {
  Capital main{};
  Capital available{};
  Capital borrowed{};
};

struct Workbook {
  WorkbookId id{};
  UserId user_id{};
  std::string name;
  Capital main_capital{};
  Capital available_capital{};
  Capital borrowed_capital{};
  WorkbookStatus status{WorkbookStatus::Active};
  Timestamp deleted_at{};
};

}  // namespace algocraft
