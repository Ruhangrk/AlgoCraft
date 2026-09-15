#pragma once

#include <cstdint>

namespace algocraft {

enum class Market : std::uint8_t { Nse = 0, Bse, Crypto };

enum class Segment : std::uint8_t { Eq = 0, Fo };

enum class InstrumentType : std::uint8_t { Equity = 0, Future, Option, Crypto, Commodity };

enum class Currency : std::uint8_t { Inr = 0 };

enum class Side : std::uint8_t { Buy = 0, Sell };

enum class OrderType : std::uint8_t { Market = 0, Limit, StopLoss, StopLossMarket };

enum class TradingMode : std::uint8_t { Mis = 0, Cnc };

enum class OrderStatus : std::uint8_t { Submitted = 0, Partial, Filled, Cancelled, Rejected };

enum class ContainerMode : std::uint8_t { Backtest = 0, Paper, Real };

enum class ContainerStatus : std::uint8_t { WarmingUp = 0, Active, Exiting, Stopped };

enum class Role : std::uint8_t { User = 0, Admin };

enum class WorkbookStatus : std::uint8_t { Active = 0, Paused, Archived };

enum class AccountStatus : std::uint8_t { Active = 0, Halted };

}  // namespace algocraft
