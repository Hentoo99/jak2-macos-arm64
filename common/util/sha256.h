#pragma once

/*!
 * @file sha256.h
 * Portable SHA-256 (FIPS 180-4), used to bind signed AOT modules to the exact
 * object file they were emitted from.  Not intended for secrets.
 */

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace sha256 {

using Digest = std::array<uint8_t, 32>;

Digest digest(std::span<const uint8_t> data);
std::string hex(const Digest& digest);
inline std::string hex_digest(std::span<const uint8_t> data) {
  return hex(digest(data));
}

}  // namespace sha256
