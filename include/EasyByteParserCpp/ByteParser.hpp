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

  template <typename T>
  T get() const {
    if constexpr (std::is_same_v<T, std::string>) return toString();
    return std::visit(
        [](auto&& arg) -> T {
          using U = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<U, std::string>)
            throw std::runtime_error("Cannot convert string value to numeric type");
          else
            return static_cast<T>(arg);
        },
        value_);
  }

  [[nodiscard]] std::string toString() const;

  [[nodiscard]] ValueType getValue() const {
    return value_;
  }

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

// Type Traits Helper for template addField
template <typename T>
struct TypeName;
template <>
struct TypeName<uint8_t> {
  static constexpr const char* value = "uint8";
};
template <>
struct TypeName<int8_t> {
  static constexpr const char* value = "int8";
};
template <>
struct TypeName<uint16_t> {
  static constexpr const char* value = "uint16";
};
template <>
struct TypeName<int16_t> {
  static constexpr const char* value = "int16";
};
template <>
struct TypeName<uint32_t> {
  static constexpr const char* value = "uint32";
};
template <>
struct TypeName<int32_t> {
  static constexpr const char* value = "int32";
};
template <>
struct TypeName<float> {
  static constexpr const char* value = "float";
};
template <>
struct TypeName<bool> {
  static constexpr const char* value = "bool";
};

class ByteParser {
 public:
  ByteParser() = default;
  ~ByteParser() = default;

  /// Load configuration from an INI file.
  /// Throws std::runtime_error if file not found or invalid format.
  /// \param configPath Path to the configuration file
  void loadConfig(const std::string& configPath);

  // --- Programmatic API ---

  /// Set the total expected length of the packet.
  ByteParser& setTotalLength(size_t length);

  /// Set the expected start code and its length.
  ByteParser& setStartCode(const std::vector<uint8_t>& code, size_t length);

  /// Set the CRC algorithm and validation field length.
  ByteParser& setCRC(const std::string& algo, size_t length);

  /// Manually add a field definition.
  ByteParser& addField(const FieldDefinition& definition);

  /// Convenience template method to add a field with inferred type string.
  /// Usage: parser.addField<float>("MyFloat", 4, ...);
  template <typename T>
  ByteParser& addField(const std::string& name, size_t byteOffset, size_t bitOffset = 0, size_t bitCount = 0,
                       bool isBigEndian = true, double scale = 1.0, double bias = 0.0) {
    FieldDefinition fd;
    fd.name = name;
    fd.byteOffset = byteOffset;
    fd.bitOffset = bitOffset;
    fd.bitCount = bitCount;
    fd.type = TypeName<T>::value;
    fd.isBigEndian = isBigEndian;
    fd.scale = scale;
    fd.bias = bias;
    return addField(fd);
  }

  /// Clear all current configurations.
  void clear();

  /// Validate the current configuration for overlaps and constraints.
  /// Called automatically by parse() if configuration changed.
  void validateConfig() const;

  // ------------------------

  /// Parse a byte buffer according to loaded configuration.
  /// Throws std::runtime_error if buffer definition is invalid (too short).
  /// \param buffer Data buffer to parse
  /// \return Map of parsed values
  std::map<std::string, ParsedValue> parse(const std::vector<char>& buffer);

  /// Parse a byte buffer according to loaded configuration.
  /// \param data Pointer to data buffer
  /// \param size Size of data buffer
  /// \return Map of parsed values
  std::map<std::string, ParsedValue> parse(const char* data, size_t size);

  static std::string dumpRaw(const std::map<std::string, ParsedValue>& data);
  static std::string dumpJson(const std::map<std::string, ParsedValue>& data);

  /// Generate a visual checklist of the current configuration.
  [[nodiscard]] std::string getConfigurationChecklist() const;

  [[nodiscard]] size_t getTotalLength() const {
    return totalLength_;
  }

  [[nodiscard]] const std::vector<uint8_t>& getStartCode() const {
    return startCode_;
  }

  [[nodiscard]] size_t getStartCodeLength() const {
    return startCodeLength_;
  }

  [[nodiscard]] std::string getCRCAlgo() const {
    return crcAlgo_;
  }

  [[nodiscard]] size_t getCRCLength() const {
    return crcLength_;
  }

 private:
  std::vector<uint8_t> startCode_;
  size_t startCodeLength_ = 0;
  size_t totalLength_ = 0;
  std::string crcAlgo_;
  size_t crcLength_ = 0;
  std::vector<FieldDefinition> fields_;
};
}  // namespace easy_byte_parser
