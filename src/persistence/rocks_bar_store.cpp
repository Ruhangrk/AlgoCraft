#include "algocraft/persistence/rocks_bar_store.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "algocraft/persistence/packed_bars.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wpedantic"
#include "rocksdb/db.h"
#pragma GCC diagnostic pop

namespace algocraft {

class RocksBarStore::Impl {
public:
  std::unique_ptr<rocksdb::DB> db{};
};

RocksBarStore::RocksBarStore(std::filesystem::path dir) : dir_{std::move(dir)} {}

RocksBarStore::~RocksBarStore() { close(); }

void RocksBarStore::open() {
  if (impl_ && impl_->db) {
    return;
  }
  if (!dir_.empty()) {
    std::filesystem::create_directories(dir_);
  }

  rocksdb::Options options;
  options.create_if_missing = true;
  rocksdb::DB* raw = nullptr;
  const auto st = rocksdb::DB::Open(options, dir_.string(), &raw);
  if (!st.ok()) {
    delete raw;
    throw std::runtime_error("rocksdb open failed: " + st.ToString());
  }
  impl_ = std::make_unique<Impl>();
  impl_->db.reset(raw);
}

void RocksBarStore::close() { impl_.reset(); }

bool RocksBarStore::is_open() const { return impl_ && impl_->db; }

void RocksBarStore::ensure_open() const {
  if (!is_open()) {
    throw std::runtime_error("rocksdb is closed");
  }
}

void RocksBarStore::put_session(std::string_view ticker, BarResolution resolution, SessionDate date,
                                const std::vector<BarEvent>& bars) {
  ensure_open();
  if (ticker.empty() || !date.ok()) {
    throw std::invalid_argument("put_session requires ticker and date");
  }
  const auto key = make_session_key(ticker, resolution, date);
  const auto value = pack_session_bars(bars);
  rocksdb::WriteOptions wo;
  wo.sync = true;
  const auto st = impl_->db->Put(wo, key, value);
  if (!st.ok()) {
    throw std::runtime_error("rocksdb put failed: " + st.ToString());
  }
}

std::optional<std::vector<BarEvent>> RocksBarStore::get_session(std::string_view ticker,
                                                                BarResolution resolution,
                                                                SessionDate date,
                                                                SymbolId symbol_id) const {
  ensure_open();
  const auto key = make_session_key(ticker, resolution, date);
  std::string value;
  const auto st = impl_->db->Get(rocksdb::ReadOptions(), key, &value);
  if (st.IsNotFound()) {
    return std::nullopt;
  }
  if (!st.ok()) {
    throw std::runtime_error("rocksdb get failed: " + st.ToString());
  }
  return unpack_session_bars(value, symbol_id, resolution);
}

bool RocksBarStore::has_session(std::string_view ticker, BarResolution resolution,
                                SessionDate date) const {
  return get_session(ticker, resolution, date, 0).has_value();
}

std::vector<SessionDate> RocksBarStore::list_sessions(std::string_view ticker,
                                                      BarResolution resolution, SessionDate from,
                                                      SessionDate to) const {
  ensure_open();
  std::string prefix;
  prefix.append(ticker);
  prefix.push_back('|');
  prefix.append(bar_resolution_code(resolution));
  prefix.push_back('|');

  const auto start = from.ok() ? make_session_key(ticker, resolution, from) : prefix;
  const auto end = to.ok() ? make_session_key(ticker, resolution, to) : std::string{};

  std::unique_ptr<rocksdb::Iterator> it{impl_->db->NewIterator(rocksdb::ReadOptions())};
  std::vector<SessionDate> out;
  for (it->Seek(start); it->Valid(); it->Next()) {
    const auto key = it->key().ToString();
    if (!key.starts_with(prefix)) {
      break;
    }
    if (!end.empty() && key > end) {
      break;
    }
    const auto pos = key.rfind('|');
    if (pos == std::string::npos || pos + 1 >= key.size()) {
      continue;
    }
    out.push_back(SessionDate::from_iso(key.substr(pos + 1)));
  }
  if (!it->status().ok()) {
    throw std::runtime_error("rocksdb iterate failed: " + it->status().ToString());
  }
  return out;
}

}  // namespace algocraft
