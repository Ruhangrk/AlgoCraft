#pragma once

#include <cstdint>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

struct OrderIntent {
  StrategyId strategy_id{};
  SymbolId symbol_id{0};
  Side side{Side::Buy};
  Quantity quantity{};
  OrderType type{OrderType::Market};
  Price price{};
};

struct FillEvent {
  OrderId order_id{};
  SymbolId symbol_id{0};
  Side side{Side::Buy};
  Quantity filled_qty{};
  Price fill_price{};
  Capital fees{};
  Capital net_cash_impact{};
  Timestamp timestamp{};
  WorkbookId workbook_id{};
};

struct OrderUpdate {
  OrderId order_id{};
  OrderStatus status{OrderStatus::Submitted};
};

enum class SystemEventType : std::uint8_t {
  SessionStart = 0,
  SessionEnd,
  KillSwitch,
  MisSquareoffWarning,
};

struct SystemEvent {
  SystemEventType type{SystemEventType::SessionStart};
  Timestamp timestamp{};
};

enum class WorkbookEventType : std::uint8_t {
  Created = 0,
  CapitalAdded,
  ActivityBorrow,
  ActivityReturn,
};

struct WorkbookEvent {
  WorkbookEventType type{WorkbookEventType::Created};
  WorkbookId workbook_id{};
  Capital amount{};
  Timestamp timestamp{};
};

}  // namespace algocraft
