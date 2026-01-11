#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace easy_byte_parser {

class ParsedValue {
public:
  using ValueType = std::variant<uint64_t, int64_t, double, bool, std::string>;

  ParsedValue() = default;
  ParsedValue(ValueType v) : value_(std::move(v)) {}

  template <typename T> T get() const {
    if constexpr (std::is_same_v<T, std::string>) {
      return toString();
    } else {
      return std::visit(
          [](auto &&arg) -> T {
            using U = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<U, std::string>)
              throw std::runtime_error(
                  "Cannot convert string value to numeric type");
            else
              return static_cast<T>(arg);
          },
          value_);
    }
  }

  std::string toString() const;
  ValueType getValue() const { return value_; }

private:
  ValueType value_;
};

struct FieldDefinition {
  std::string name;
  size_t byteOffset = 0;
  size_t bitOffset = 0;
  size_t bitCount = 0;
  std::string type = "uint8";
  bool isBigEndian = true;
  double scale = 1.0;
  double bias = 0.0;
};

class ByteParser {
public:
  ByteParser() = default;
  ~ByteParser() = default;

  /**
   * @brief Load configuration from an INI file.
   * Throws std::runtime_error if file not found or invalid format.
   */
  void loadConfig(const std::string &configPath);

  /**
   * @brief Parse a byte buffer according to loaded configuration.
   * Throws std::runtime_error if buffer definition is invalid (too short).
   */
  std::map<std::string, ParsedValue> parse(const std::vector<char> &buffer);
  std::map<std::string, ParsedValue> parse(const char *data, size_t size);

  static std::string dumpRaw(const std::map<std::string, ParsedValue> &data);
  static std::string dumpJson(const std::map<std::string, ParsedValue> &data);

  size_t getTotalLength() const { return totalLength_; }
  const std::vector<uint8_t> &getStartCode() const { return startCode_; }
  size_t getStartCodeLength() const { return startCodeLength_; }
  std::string getCRCAlgo() const { return crcAlgo_; }
  size_t getCRCLength() const { return crcLength_; }

private:
  std::vector<uint8_t> startCode_;
  size_t startCodeLength_ = 0;
  size_t totalLength_ = 0;
  std::string crcAlgo_;
  size_t crcLength_ = 0;
  std::vector<FieldDefinition> fields_;
};

} // namespace easy_byte_parser
