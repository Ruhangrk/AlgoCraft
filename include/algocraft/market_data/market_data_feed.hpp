#pragma once

#include <functional>
#include <optional>
#include <unordered_map>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

class MarketDataFeed {
public:
  using TickHandler = std::function<void(SymbolId symbol_id, Price ltp, Timestamp ts)>;

  virtual ~MarketDataFeed() = default;

  virtual void set_handler(TickHandler handler) { handler_ = std::move(handler); }
  virtual void connect() = 0;
  virtual void subscribe(SymbolId symbol_id) = 0;
  virtual void unsubscribe(SymbolId symbol_id) = 0;
  virtual void disconnect() {}

protected:
  void emit_tick(SymbolId symbol_id, Price ltp, Timestamp ts) {
    if (handler_) {
      handler_(symbol_id, ltp, ts);
    }
  }

  TickHandler handler_{};
};

// Aggregates LTPC ticks into closed 1-minute BarEvents (IST minute buckets).
class MinuteBarBuilder {
public:
  using BarHandler = std::function<void(const BarEvent&)>;

  explicit MinuteBarBuilder(BarHandler on_bar) : on_bar_(std::move(on_bar)) {}

  void on_tick(SymbolId symbol_id, Price ltp, Timestamp ts) {
    if (ltp.paise() <= 0) {
      return;
    }
    const auto minute = ist_minute_of_day(ts);
    const auto day_key = (ts.nanos() + kIstOffsetNs) / kNanosPerDay;
    const auto bucket = day_key * 1440 + minute;

    auto& cur = open_[symbol_id];
    if (!cur.has_value() || cur->bucket != bucket) {
      if (cur.has_value()) {
        emit(*cur, symbol_id);
      }
      cur = Partial{};
      cur->bucket = bucket;
      cur->open = ltp;
      cur->high = ltp;
      cur->low = ltp;
      cur->close = ltp;
      cur->bar_ts = floor_to_minute(ts);
      return;
    }
    if (ltp.paise() > cur->high.paise()) {
      cur->high = ltp;
    }
    if (ltp.paise() < cur->low.paise()) {
      cur->low = ltp;
    }
    cur->close = ltp;
  }

  void flush() {
    for (auto& [symbol_id, cur] : open_) {
      if (cur.has_value()) {
        emit(*cur, symbol_id);
        cur.reset();
      }
    }
  }

private:
  struct Partial {
    std::int64_t bucket{-1};
    Timestamp bar_ts{};
    Price open{};
    Price high{};
    Price low{};
    Price close{};
  };

  static Timestamp floor_to_minute(Timestamp ts) {
    const auto aligned = (ts.nanos() / kNanosPerMinute) * kNanosPerMinute;
    return Timestamp::from_nanos(aligned);
  }

  void emit(const Partial& p, SymbolId symbol_id) {
    if (!on_bar_) {
      return;
    }
    BarEvent bar{};
    bar.symbol_id = symbol_id;
    bar.timestamp = p.bar_ts;
    bar.resolution = BarResolution::OneMin;
    bar.open = p.open;
    bar.high = p.high;
    bar.low = p.low;
    bar.close = p.close;
    bar.volume = Quantity::from_shares(0);
    on_bar_(bar);
  }

  BarHandler on_bar_{};
  std::unordered_map<SymbolId, std::optional<Partial>> open_{};
};

}  // namespace algocraft
