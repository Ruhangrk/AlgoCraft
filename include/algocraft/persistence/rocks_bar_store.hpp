#pragma once

#include <filesystem>
#include <memory>

#include "algocraft/market_data/bar_store.hpp"

namespace algocraft {

class RocksBarStore final : public BarStore {
public:
  explicit RocksBarStore(std::filesystem::path dir);
  ~RocksBarStore() override;

  RocksBarStore(const RocksBarStore&) = delete;
  RocksBarStore& operator=(const RocksBarStore&) = delete;
  RocksBarStore(RocksBarStore&&) = delete;
  RocksBarStore& operator=(RocksBarStore&&) = delete;

  void open();
  void close();

  [[nodiscard]] bool is_open() const;
  [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

  void put_session(std::string_view ticker, BarResolution resolution, SessionDate date,
                   const std::vector<BarEvent>& bars) override;

  [[nodiscard]] std::optional<std::vector<BarEvent>> get_session(std::string_view ticker,
                                                                 BarResolution resolution,
                                                                 SessionDate date,
                                                                 SymbolId symbol_id) const override;

  [[nodiscard]] bool has_session(std::string_view ticker, BarResolution resolution,
                                 SessionDate date) const override;

  [[nodiscard]] std::vector<SessionDate> list_sessions(std::string_view ticker,
                                                       BarResolution resolution, SessionDate from,
                                                       SessionDate to) const override;

private:
  class Impl;

  void ensure_open() const;

  std::filesystem::path dir_{};
  std::unique_ptr<Impl> impl_{};
};

}  // namespace algocraft
