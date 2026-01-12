#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace easy_byte_parser {
namespace utils {

inline std::string trim(const std::string &str) {
  size_t first = str.find_first_not_of(" \t\n\r");
  if (std::string::npos == first) {
    return str;
  }
  size_t last = str.find_last_not_of(" \t\n\r");
  return str.substr(first, (last - first + 1));
}

inline std::vector<std::string> split(const std::string &str, char delimiter) {
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream tokenStream(str);
  while (std::getline(tokenStream, token, delimiter)) {
    std::string trimmed = trim(token);
    if (!trimmed.empty()) {
      tokens.push_back(trimmed);
    }
  }
  return tokens;
}

inline std::string toLower(const std::string &str) {
  std::string lowerStr = str;
  std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return lowerStr;
}

/// Generic byte swap
/// \param value Value to swap
/// \return Swapped value
template <typename T> inline T byteswap(T value) {
  static_assert(std::is_fundamental<T>::value,
                "byteswap requires fundamental type");
  if constexpr (sizeof(T) == 1)
    return value;

  // Use intrinsics if available or manual generic swap
  union {
    T u;
    unsigned char u8[sizeof(T)];
  } source, dest;
  source.u = value;
  for (size_t k = 0; k < sizeof(T); k++)
    dest.u8[k] = source.u8[sizeof(T) - k - 1];
  return dest.u;
}

inline bool isBigEndianSystem() {
  const int value = 1;
  return (*reinterpret_cast<const char *>(&value)) == 0;
}

/// Read implementation
/// \param data Source data pointer
/// \param isBigEndianSource True if source data is big-endian
/// \return Read value
template <typename T>
T readFromBuffer(const char *data, bool isBigEndianSource) {
  T value;
  std::memcpy(&value, data, sizeof(T));

  bool systemBigEndian = isBigEndianSystem();
  if (isBigEndianSource != systemBigEndian) {
    return byteswap(value);
  }
  return value;
}

/// Calculate CRC16-MODBUS
/// \param data Pointer to data buffer
/// \param length Length of data
/// \return CRC16 value (little-endian)
inline uint16_t calculateCRC16Modbus(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc = crc >> 1;
      }
    }
  }

  return crc;
}

} // namespace utils
} // namespace easy_byte_parser
