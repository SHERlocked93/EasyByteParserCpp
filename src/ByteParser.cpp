#include "EasyByteParserCpp/ByteParser.hpp"

#include "Utils.hpp"

// Third-party libraries
#define MINI_CASE_SENSITIVE
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

#include "3rdparty/mini/ini.h"
#include "3rdparty/nlohmann/json.hpp"

namespace easy_byte_parser {

// ParsedValue Implementation
std::string ParsedValue::toString() const {
  return std::visit(
      [](auto &&arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>)
          return arg;
        else if constexpr (std::is_same_v<T, bool>)
          return arg ? "true" : "false";
        else
          return std::to_string(arg);
      },
      value_);
}

// Helpers
static bool isValidType(const std::string &t) {
  static const std::set<std::string> valid = {
      "uint8", "int8", "uint16", "int16", "uint32", "int32", "float", "bool"};
  return valid.find(t) != valid.end();
}

static size_t getTypeSize(const std::string &t) {
  if (t == "uint8" || t == "int8" || t == "bool")
    return 1;
  if (t == "uint16" || t == "int16")
    return 2;
  if (t == "uint32" || t == "int32" || t == "float")
    return 4;
  return 0;
}

void ByteParser::loadConfig(const std::string &configPath) {
  mINI::INIFile file(configPath);
  mINI::INIStructure ini;

  if (!file.read(ini)) {
    throw std::runtime_error(
        "Config file not found or unreadable or invalid INI: " + configPath);
  }

  // 1. Header
  if (!ini.has("Header")) {
    throw std::runtime_error("Missing [Header] section in " + configPath);
  }

  auto &header = ini["Header"];

  // Strict Pairing Check for StartCode / StartCodeLength
  bool hasSC = header.has("StartCode");
  bool hasSCL = header.has("StartCodeLength");

  startCode_.clear();
  startCodeLength_ = 0;

  if (hasSC != hasSCL) {
    std::cerr << "Warning: StartCode and StartCodeLength must appear in pairs. "
                 "Discarding StartCode configuration."
              << std::endl;
  } else if (hasSC) {
    // Both present
    std::string hexCode = header["StartCode"];
    for (size_t i = 0; i < hexCode.length(); i += 2) {
      if (i + 1 >= hexCode.length())
        break;
      std::string byteStr = hexCode.substr(i, 2);
      try {
        startCode_.push_back((uint8_t)std::stoul(byteStr, nullptr, 16));
      } catch (...) {
        throw std::runtime_error("Invalid StartCode hex: " + hexCode);
      }
    }

    try {
      startCodeLength_ = std::stoul(header["StartCodeLength"]);
    } catch (...) {
      throw std::runtime_error("Invalid StartCodeLength");
    }

    // Logic check: Header size vs Code size
    if (startCode_.size() > startCodeLength_) {
      throw std::runtime_error("StartCode binary size exceeds StartCodeLength");
    }
  }

  if (!header.has("TotalLength"))
    throw std::runtime_error("Missing Header.TotalLength");
  try {
    totalLength_ = std::stoul(header["TotalLength"]);
  } catch (...) {
    throw std::runtime_error("Invalid TotalLength");
  }

  // Strict Pairing Check for CRCAlgo / CRCLength
  bool hasCRC = header.has("CRCAlgo");
  bool hasCRCL = header.has("CRCLength");

  crcAlgo_ = "";
  crcLength_ = 0;

  if (hasCRC != hasCRCL) {
    std::cerr << "Warning: CRCAlgo and CRCLength must appear in pairs. "
                 "Discarding CRC configuration."
              << std::endl;
  } else if (hasCRC) {
    crcAlgo_ = header["CRCAlgo"];
    try {
      crcLength_ = std::stoul(header["CRCLength"]);
    } catch (...) {
      throw std::runtime_error("Invalid CRCLength");
    }
  }

  // 2. Fields
  fields_.clear();

  // Overlap Detection Map (BitMask per byte)
  // 0xFF means fully occupied or utilized.
  std::vector<uint8_t> usageMap(totalLength_, 0);

  // Mark StartCode Region
  if (startCodeLength_ > 0) {
    if (startCodeLength_ > totalLength_)
      throw std::runtime_error("StartCodeLength exceeds TotalLength");
    for (size_t i = 0; i < startCodeLength_; ++i)
      usageMap[i] = 0xFF;
  }

  // Mark CRC Region
  if (crcLength_ > 0) {
    if (crcLength_ > totalLength_)
      throw std::runtime_error("CRCLength exceeds TotalLength");
    size_t crcStart = totalLength_ - crcLength_;
    // Ensure CRC doesn't overlap StartCode (Header consistency)
    if (startCodeLength_ > 0 && crcStart < startCodeLength_) {
      throw std::runtime_error("CRCLength overlaps with StartCodeLength");
    }
    for (size_t i = crcStart; i < totalLength_; ++i)
      usageMap[i] = 0xFF;
  }

  // Iterate all sections
  for (auto &[sectionName, collection] : ini) {
    if (sectionName == "Header")
      continue;

    // Check if it's a field (has ByteOffset)
    if (collection.has("ByteOffset")) {
      FieldDefinition f;
      f.name = sectionName;

      try {
        f.byteOffset = std::stoul(collection.get("ByteOffset"));

        if (collection.has("BitOffset"))
          f.bitOffset = std::stoul(collection.get("BitOffset"));
        if (collection.has("BitCount"))
          f.bitCount = std::stoul(collection.get("BitCount"));

        f.type = collection.has("Type") ? collection.get("Type") : "uint8";

        if (!isValidType(f.type)) {
          throw std::runtime_error("Field [" + sectionName +
                                   "] has invalid Type: " + f.type);
        }

        if (collection.has("Endian")) {
          std::string end = utils::toLower(collection.get("Endian"));
          f.isBigEndian = (end == "big");
        }

        if (collection.has("Scale"))
          f.scale = std::stod(collection.get("Scale"));
        if (collection.has("Bias"))
          f.bias = std::stod(collection.get("Bias"));

        size_t size = getTypeSize(f.type);
        if (f.byteOffset + size > totalLength_) {
          throw std::runtime_error("Field [" + sectionName +
                                   "] ByteOffset exceeds TotalLength");
        }

        // Bit Logic validation
        size_t typeBits = size * 8;
        if (f.bitCount > 0 && f.bitOffset + f.bitCount > typeBits) {
          throw std::runtime_error("Field [" + sectionName +
                                   "] Bit logic exceeds type width");
        }

        // Overlap Detection Logic
        uint64_t fieldFullMask = 0;
        if (f.bitCount == 0) {
          fieldFullMask = ~0ULL;
        } else {
          // Create mask: (1 << Count) - 1 << Offset
          uint64_t ones =
              (f.bitCount == 64) ? ~0ULL : ((1ULL << f.bitCount) - 1);
          fieldFullMask = ones << f.bitOffset;
        }

        for (size_t i = 0; i < size; ++i) {
          size_t absByteIndex = f.byteOffset + i;

          size_t bitStart;
          if (f.isBigEndian) {
            // MSB at first byte
            size_t bytePosFromLSB = size - 1 - i;
            bitStart = bytePosFromLSB * 8;
          } else {
            size_t bytePosFromLSB = i;
            bitStart = bytePosFromLSB * 8;
          }

          // shift fieldFullMask to align bitStart to bit 0 of this byte
          // Actually, we want to extract bits [bitStart, bitStart+7] from
          // fieldFullMask And put them into byteMask [0, 7]
          uint8_t byteMask = (uint8_t)((fieldFullMask >> bitStart) & 0xFF);

          if (byteMask == 0)
            continue;

          // Collision Check
          if (usageMap[absByteIndex] & byteMask) {
            std::string reason;
            if (startCodeLength_ > 0 && absByteIndex < startCodeLength_) {
              reason = "overlaps with StartCode header region";
            } else if (crcLength_ > 0 &&
                       absByteIndex >= (totalLength_ - crcLength_)) {
              reason = "overlaps with CRC tail region";
            } else {
              reason = "overlaps with another field";
            }
            throw std::runtime_error("Field [" + sectionName +
                                     "] collision: " + reason);
          }

          // Mark usage
          usageMap[absByteIndex] |= byteMask;
        }

        fields_.push_back(f);

      } catch (const std::exception &e) {
        throw std::runtime_error("Error parsing field [" + sectionName +
                                 "]: " + e.what());
      }
    }
  }
}

std::map<std::string, ParsedValue>
ByteParser::parse(const std::vector<char> &buffer) {
  return parse(buffer.data(), buffer.size());
}

std::map<std::string, ParsedValue> ByteParser::parse(const char *data,
                                                     size_t size) {
  if (size < totalLength_) {
    // Option: throw or return partial? Original threw only if accessed? No it
    // iterated. Let's safe guard. throw std::runtime_error("Buffer size " +
    // std::to_string(size) + " smaller than expected " +
    // std::to_string(totalLength_)); Actually user might pass smaller buffer if
    // it's a partial packet, but for parser logic we need full data designated
    // by TotalLength
  }

  std::map<std::string, ParsedValue> result;

  for (const auto &field : fields_) {
    if (field.byteOffset + getTypeSize(field.type) > size) {
      continue; // or throw? Skip safely.
    }

    const char *ptr = data + field.byteOffset;
    ParsedValue val;

    // Helper lambda to read and scale
    auto process =
        [&](auto dummyType) -> std::variant<uint64_t, int64_t, double, bool> {
      using T = decltype(dummyType);
      T raw = utils::readFromBuffer<T>(ptr, field.isBigEndian);

      // Bit logic
      if (field.bitCount > 0) {
        // Bits usually apply to unsigned integers
        uint64_t mask = (1ULL << field.bitCount) - 1;
        uint64_t v = (uint64_t)raw;
        v = (v >> field.bitOffset) & mask;

        // If original was bool, cast back
        if constexpr (std::is_same_v<T, bool>)
          return v != 0;
        // If original was float, this path is weird but let's assume bits apply
        // to int representation
        return (double)((double)v * field.scale +
                        field.bias); // Treat bit extracted value as number
      }

      if constexpr (std::is_floating_point_v<T>) {
        return (double)raw * field.scale + field.bias;
      } else if constexpr (std::is_same_v<T, bool>) {
        return raw;
      } else {
        // Integer
        // Scale only applies if result is cast to double usually?
        // Or does it return double?
        // In original logic: if scale != 1.0 or bias != 0.0, we probably wanted
        // double result
        if (field.scale != 1.0 || field.bias != 0.0) {
          return (double)raw * field.scale + field.bias;
        }
        return (int64_t)raw; // or uint64_t
      }
    };

    if (field.type == "uint8") {
      auto v = process(uint8_t{});
      if (std::holds_alternative<double>(v))
        val = ParsedValue(std::get<double>(v));
      else
        val = ParsedValue((uint64_t)std::get<int64_t>(
            v)); // Cast generic int64 back to uint64 if needed?
    } else if (field.type == "int8") {
      val = ParsedValue(std::get<int64_t>(
          process(int8_t{}))); // Simplified, variant handling needs care
    } else if (field.type == "uint16") {
      // Let's refine the process return to be ParsedValue directly to simplify
      uint16_t raw = utils::readFromBuffer<uint16_t>(ptr, field.isBigEndian);

      if (field.bitCount > 0) {
        uint16_t mask = (1 << field.bitCount) - 1;
        raw = (raw >> field.bitOffset) & mask;
      }

      if (field.scale != 1.0 || field.bias != 0.0) {
        val = ParsedValue((double)raw * field.scale + field.bias);
      } else {
        val = ParsedValue((uint64_t)raw);
      }
    }
    // ... (Repeating for all types is verbose. Let's do a switch/generic
    // approach properly)

    // Proper Generic Approach
    if (field.type == "float") {
      float raw = utils::readFromBuffer<float>(ptr, field.isBigEndian);
      val = ParsedValue((double)raw * field.scale + field.bias);
    } else if (field.type == "bool") {
      uint8_t raw = utils::readFromBuffer<uint8_t>(ptr, field.isBigEndian);
      if (field.bitCount > 0)
        raw = (raw >> field.bitOffset) & 1;
      val = ParsedValue((bool)raw);
    } else {
      // Integers
      int64_t iVal = 0;
      uint64_t uVal = 0;
      bool isSigned = (field.type[0] == 'i');

      if (field.type == "uint8")
        uVal = utils::readFromBuffer<uint8_t>(ptr, field.isBigEndian);
      else if (field.type == "int8")
        iVal = utils::readFromBuffer<int8_t>(ptr, field.isBigEndian);
      else if (field.type == "uint16")
        uVal = utils::readFromBuffer<uint16_t>(ptr, field.isBigEndian);
      else if (field.type == "int16")
        iVal = utils::readFromBuffer<int16_t>(ptr, field.isBigEndian);
      else if (field.type == "uint32")
        uVal = utils::readFromBuffer<uint32_t>(ptr, field.isBigEndian);
      else if (field.type == "int32")
        iVal = utils::readFromBuffer<int32_t>(ptr, field.isBigEndian);

      // Bit Operation always on unsigned representation usually?
      if (field.bitCount > 0) {
        if (isSigned)
          uVal = (uint64_t)iVal; // treat as bits
        uVal = (uVal >> field.bitOffset) & ((1ULL << field.bitCount) - 1);
        // After extraction, is it signed? Usually bitfields are unsigned unless
        // specified. let's assume result is unsigned
        isSigned = false;
      }

      if (field.scale != 1.0 || field.bias != 0.0) {
        double d = isSigned ? (double)iVal : (double)uVal;
        val = ParsedValue(d * field.scale + field.bias);
      } else {
        if (isSigned)
          val = ParsedValue(iVal);
        else
          val = ParsedValue(uVal);
      }
    }

    result[field.name] = val;
  }

  return result;
}

std::string
ByteParser::dumpRaw(const std::map<std::string, ParsedValue> &data) {
  std::stringstream ss;
  ss << "Data Dump:\n";
  for (const auto &[key, val] : data) {
    ss << key << " = " << val.toString() << "\n";
  }
  return ss.str();
}

std::string
ByteParser::dumpJson(const std::map<std::string, ParsedValue> &data) {
  nlohmann::json j;
  // We need to unflatten the keys "temp.engine_oil" -> {"temp": {"engine_oil":
  // val}}

  for (const auto &[key, val] : data) {
    std::vector<std::string> parts = utils::split(key, '.');
    nlohmann::json *curr = &j;
    for (size_t i = 0; i < parts.size() - 1; ++i) {
      curr = &((*curr)[parts[i]]);
    }

    // Final value
    std::visit([&](auto &&arg) { (*curr)[parts.back()] = arg; },
               val.getValue());
  }
  return j.dump(4); // Pretty print
}

} // namespace easy_byte_parser
