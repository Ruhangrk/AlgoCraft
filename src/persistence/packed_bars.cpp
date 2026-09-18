#include "algocraft/persistence/packed_bars.hpp"

#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"

#include <cstdint>
#include <stdexcept>

namespace algocraft {
namespace {

constexpr char kMagic[] = {'A', 'C', 'B', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kRecordSize = 48;

void append_le32(std::string& out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<char>(value & 0xffu));
    value >>= 8;
  }
}

void append_le64(std::string& out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<char>(value & 0xffu));
    value >>= 8;
  }
}

std::uint32_t read_le32(std::string_view blob, std::size_t offset) {
  std::uint32_t value = 0;
  for (int i = 3; i >= 0; --i) {
    value = (value << 8) | static_cast<std::uint8_t>(blob[offset + static_cast<std::size_t>(i)]);
  }
  return value;
}

std::uint64_t read_le64(std::string_view blob, std::size_t offset) {
  std::uint64_t value = 0;
  for (int i = 7; i >= 0; --i) {
    value = (value << 8) | static_cast<std::uint8_t>(blob[offset + static_cast<std::size_t>(i)]);
  }
  return value;
}

}  // namespace

std::string pack_session_bars(const std::vector<BarEvent>& bars) {
  std::string out;
  out.reserve(kHeaderSize + bars.size() * kRecordSize);
  out.append(kMagic, 4);
  append_le32(out, kVersion);
  append_le32(out, static_cast<std::uint32_t>(bars.size()));
  for (const auto& bar : bars) {
    append_le64(out, static_cast<std::uint64_t>(bar.timestamp.nanos()));
    append_le64(out, static_cast<std::uint64_t>(bar.open.paise()));
    append_le64(out, static_cast<std::uint64_t>(bar.high.paise()));
    append_le64(out, static_cast<std::uint64_t>(bar.low.paise()));
    append_le64(out, static_cast<std::uint64_t>(bar.close.paise()));
    append_le64(out, static_cast<std::uint64_t>(bar.volume.shares()));
  }
  return out;
}

std::vector<BarEvent> unpack_session_bars(std::string_view blob, SymbolId symbol_id,
                                          BarResolution resolution) {
  if (blob.size() < kHeaderSize) {
    throw std::runtime_error("bar blob too short");
  }
  if (blob[0] != kMagic[0] || blob[1] != kMagic[1] || blob[2] != kMagic[2] || blob[3] != kMagic[3]) {
    throw std::runtime_error("bar blob magic mismatch");
  }
  const auto version = read_le32(blob, 4);
  if (version != kVersion) {
    throw std::runtime_error("bar blob version mismatch");
  }
  const auto count = read_le32(blob, 8);
  if (blob.size() != kHeaderSize + static_cast<std::size_t>(count) * kRecordSize) {
    throw std::runtime_error("bar blob size mismatch");
  }

  std::vector<BarEvent> bars;
  bars.reserve(count);
  std::size_t off = kHeaderSize;
  for (std::uint32_t i = 0; i < count; ++i) {
    BarEvent bar{};
    bar.symbol_id = symbol_id;
    bar.resolution = resolution;
    bar.timestamp = Timestamp::from_nanos(static_cast<std::int64_t>(read_le64(blob, off)));
    bar.open = Price::from_paise(static_cast<std::int64_t>(read_le64(blob, off + 8)));
    bar.high = Price::from_paise(static_cast<std::int64_t>(read_le64(blob, off + 16)));
    bar.low = Price::from_paise(static_cast<std::int64_t>(read_le64(blob, off + 24)));
    bar.close = Price::from_paise(static_cast<std::int64_t>(read_le64(blob, off + 32)));
    bar.volume = Quantity::from_shares(static_cast<std::int64_t>(read_le64(blob, off + 40)));
    bars.push_back(bar);
    off += kRecordSize;
  }
  return bars;
}

}  // namespace algocraft
