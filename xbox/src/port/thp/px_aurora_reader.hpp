// px_aurora_reader.hpp - the part of aurora's lib/internal.hpp its THP decoder
// uses (aurora::ByteReader: bounded big-endian reads), without its logging
// and C++23 pieces (nxdk's libc++ has no std::bit_cast).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#include "xhw.h"

// A big-endian value at ptr (unaligned), as aurora's read_bits.
template < typename T >
inline T read_bits(const void* ptr) noexcept {
  static_assert(std::is_integral_v< T >, "integers only");
  using U = std::make_unsigned_t< T >;
  const uint8_t* p = static_cast< const uint8_t* >(ptr);
  U v = 0;
  for (size_t i = 0; i < sizeof(T); ++i)
    v = static_cast< U >((v << 8) | p[i]);
  return static_cast< T >(v);
}

namespace aurora {

class ByteReader {
public:
  ByteReader(const uint8_t* data, size_t size) noexcept : mData{data}, mSize{size} {}

  size_t offset() const noexcept { return mPosition; }
  size_t remaining() const noexcept { return mSize - mPosition; }

  template < typename T >
  bool try_read(T& value) noexcept {
    static_assert(std::is_integral_v< T >, "integers only");
    if (!can_read(sizeof(T)))
      return false;
    using U = std::make_unsigned_t< T >;
    U v = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
      v = static_cast< U >((v << 8) | mData[mPosition + i]);
    mPosition += sizeof(T);
    value = static_cast< T >(v);
    return true;
  }

  template < typename T >
  T read() noexcept {
    T v{};
    if (!try_read(v))
      xhw_fatal("Movie error", "THP reader overrun");
    return v;
  }

  bool try_take(size_t count, std::span< const uint8_t >& bytes) noexcept {
    if (!can_read(count))
      return false;
    bytes = std::span< const uint8_t >{mData + mPosition, count};
    mPosition += count;
    return true;
  }

  void skip(size_t count) noexcept {
    if (!can_read(count))
      xhw_fatal("Movie error", "THP reader overrun");
    mPosition += count;
  }

private:
  bool can_read(size_t count) const noexcept { return mPosition <= mSize && count <= mSize - mPosition; }

  const uint8_t* mData;
  size_t mSize;
  size_t mPosition = 0;
};

} // namespace aurora
