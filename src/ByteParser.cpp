#include "EasyByteParserCpp/ByteParser.hpp"

#include "Utils.hpp"

#define MINI_CASE_SENSITIVE
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

#include "3rdparty/mini/ini.h"
#include "3rdparty/nlohmann/json.hpp"

namespace easy_byte_parser {

std::string ParsedValue::toString() const {
  return std::visit(
      [](auto&& arg) -> std::string {
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

static bool isValidType(const std::string& t) {
  static const std::set<std::string> valid = {"uint8", "int8", "uint16", "int16", "uint32", "int32", "float", "bool"};
  return valid.find(t) != valid.end();
}

static size_t getTypeSize(const std::string& t) {
  if (t == "uint8" || t == "int8" || t == "bool") return 1;
  if (t == "uint16" || t == "int16") return 2;
  if (t == "uint32" || t == "int32" || t == "float") return 4;
  return 0;
}

// --- Programmatic API Implementation ---

ByteParser& ByteParser::setTotalLength(size_t length) {
  totalLength_ = length;
  return *this;
}

ByteParser& ByteParser::setStartCode(const std::vector<uint8_t>& code, size_t length) {
  startCode_ = code;
  startCodeLength_ = length;
  return *this;
}

ByteParser& ByteParser::setCRC(const std::string& algo, size_t length) {
  crcAlgo_ = algo;
  crcLength_ = length;
  return *this;
}

ByteParser& ByteParser::addField(const FieldDefinition& definition) {
  // Basic sanity check on type
  if (!isValidType(definition.type)) {
    throw std::runtime_error("[EasyByteParserCpp]: Invalid type for field " + definition.name + ": " + definition.type);
  }
  fields_.push_back(definition);
  return *this;
}

void ByteParser::clear() {
  totalLength_ = 0;
  startCode_.clear();
  startCodeLength_ = 0;
  crcAlgo_.clear();
  crcLength_ = 0;
  fields_.clear();
}

void ByteParser::validateConfig() const {
  if (totalLength_ == 0) {
    throw std::runtime_error("[EasyByteParserCpp]: TotalLength must be greater than 0");
  }

  // Header Validation
  if (!startCode_.empty()) {
    if (startCode_.size() > startCodeLength_) {
      throw std::runtime_error("[EasyByteParserCpp]: StartCode binary size exceeds StartCodeLength");
    }
  }

  // CRC Validation
  if (!crcAlgo_.empty()) {
    if (crcAlgo_ == "CRC16" && crcLength_ != 2) {
      throw std::runtime_error("[EasyByteParserCpp]: CRC16 algorithm requires CRCLength=2");
    }
  }

  // Bounds & Overlap Validation (Bit-level precision)
  std::vector<int> bitOwner(totalLength_ * 8, -1);  // -1: unused, >=0: field index, -2: CRC

  // Mark CRC region
  if (!crcAlgo_.empty() && crcLength_ > 0) {
    if (totalLength_ >= crcLength_) {
      size_t crcStartBits = (totalLength_ - crcLength_) * 8;
      for (size_t i = crcStartBits; i < totalLength_ * 8; ++i) bitOwner[i] = -2;
    }
  }

  for (size_t i = 0; i < fields_.size(); ++i) {
    const auto& f = fields_[i];
    size_t sz = getTypeSize(f.type);

    // Bounds check (Byte level first for simplicity)
    if (f.byteOffset + sz > totalLength_) {
      throw std::runtime_error("[EasyByteParserCpp]: Field " + f.name + " exceeds TotalLength");
    }

    // Determine Bit Range
    size_t startBit = f.byteOffset * 8;
    size_t endBit = startBit + sz * 8;

    if (f.bitCount > 0) {
      size_t typeBits = sz * 8;
      if (f.bitOffset + f.bitCount > typeBits) {
        throw std::runtime_error("[EasyByteParserCpp]: Bit logic exceeds type width for field " + f.name);
      }
      startBit = f.byteOffset * 8 + f.bitOffset;
      endBit = startBit + f.bitCount;
    }

    // Check overlap
    for (size_t b = startBit; b < endBit; ++b) {
      if (b >= totalLength_ * 8)
        throw std::runtime_error("[EasyByteParserCpp]: Field " + f.name +
                                 " out of bounds");  // Should be caught by byte check usually

      int owner = bitOwner[b];
      if (owner != -1) {
        if (owner == -2)
          throw std::runtime_error("[EasyByteParserCpp]: Field " + f.name + " overlaps with CRC");
        else
          throw std::runtime_error("[EasyByteParserCpp]: Overlap detected for field " + f.name);
      }
      bitOwner[b] = (int)i;
    }
  }
}

// --- Legacy / INI Loader ---

void ByteParser::loadConfig(const std::string& configPath) {
  clear();  // Reset first

  mINI::INIFile file(configPath);
  mINI::INIStructure ini;

  if (!file.read(ini)) {
    throw std::runtime_error("[EasyByteParserCpp]: Config file not found or unreadable or invalid INI: " + configPath);
  }

  // 1. Header
  if (!ini.has("Header")) {
    throw std::runtime_error("[EasyByteParserCpp]: Missing [Header] section in " + configPath);
  }

  auto& header = ini["Header"];

  if (!header.has("TotalLength")) throw std::runtime_error("[EasyByteParserCpp]: Missing Header.TotalLength");
  setTotalLength(std::stoul(header["TotalLength"]));

  // StartCode
  bool hasSC = header.has("StartCode");
  bool hasSCL = header.has("StartCodeLength");

  if (hasSC != hasSCL) {
    std::cerr << "[EasyByteParserCpp Warning]: StartCode and StartCodeLength must appear in pairs.\n";
  } else if (hasSC) {
    std::string hexCode = header["StartCode"];
    std::vector<uint8_t> sc;
    for (size_t i = 0; i < hexCode.length(); i += 2) {
      if (i + 1 >= hexCode.length()) break;
      std::string byteStr = hexCode.substr(i, 2);
      try {
        sc.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
      } catch (...) {
        throw std::runtime_error("[EasyByteParserCpp]: Invalid StartCode hex: " + hexCode);
      }
    }
    size_t scl = std::stoul(header["StartCodeLength"]);
    setStartCode(sc, scl);
  }

  // CRC
  if (header.has("CRCAlgo") && header.has("CRCLength")) {
    setCRC(header["CRCAlgo"], std::stoul(header["CRCLength"]));
  }

  // 2. Fields
  for (auto const& it : ini) {
    if (it.first == "Header") continue;

    // Section name is Field Name
    auto& section = it.second;
    FieldDefinition fd;
    fd.name = it.first;

    if (!section.has("ByteOffset"))
      throw std::runtime_error("[EasyByteParserCpp]: Missing ByteOffset for field " + fd.name);
    if (!section.has("Type")) throw std::runtime_error("[EasyByteParserCpp]: Missing Type for field " + fd.name);

    fd.byteOffset = std::stoul(section.get("ByteOffset"));  // const wrapper fix
    fd.type = section.get("Type");

    if (!isValidType(fd.type)) throw std::runtime_error("[EasyByteParserCpp]: Invalid Type: " + fd.type);

    if (section.has("BitOffset")) fd.bitOffset = std::stoul(section.get("BitOffset"));
    if (section.has("BitCount")) fd.bitCount = std::stoul(section.get("BitCount"));

    if (section.has("Endian")) {
      if (utils::toLower(section.get("Endian")) == "little")
        fd.isBigEndian = false;
      else
        fd.isBigEndian = true;
    }

    if (section.has("Scale")) fd.scale = std::stod(section.get("Scale"));
    if (section.has("Bias")) fd.bias = std::stod(section.get("Bias"));

    addField(fd);
  }

  validateConfig();
}

std::map<std::string, ParsedValue> ByteParser::parse(const std::vector<char>& buffer) {
  if (buffer.empty()) throw std::runtime_error("[EasyByteParserCpp]: Empty buffer");
  return parse(buffer.data(), buffer.size());
}

std::map<std::string, ParsedValue> ByteParser::parse(const char* data, size_t size) {
  // Ensure valid configuration
  validateConfig();

  if (size < totalLength_) {
    throw std::runtime_error("[EasyByteParserCpp]: Buffer size (" + std::to_string(size) +
                             ") < Configured TotalLength (" + std::to_string(totalLength_) + ")");
  }

  // StartCode Check
  if (!startCode_.empty()) {
    if (size < startCode_.size()) {
      throw std::runtime_error("[EasyByteParserCpp]: Buffer too small for StartCode");
    }
    for (size_t i = 0; i < startCode_.size(); ++i) {
      if (static_cast<uint8_t>(data[i]) != startCode_[i]) {
        std::stringstream ss;
        ss << "[EasyByteParserCpp]: Invalid Start Code at byte " << i << ". Expected 0x" << std::hex << std::setw(2)
           << std::setfill('0') << (int)startCode_[i] << " but got 0x" << (int)(uint8_t)data[i];
        throw std::runtime_error(ss.str());
      }
    }
  }

  // CRC Check
  if (!crcAlgo_.empty() && crcLength_ > 0) {
    if (size < crcLength_) {
      throw std::runtime_error("[EasyByteParserCpp]: Buffer too small for CRC check");
    }

    if (crcAlgo_ == "CRC16") {
      // Calculate CRC on data range: [0, TotalLength - CRCLength)
      size_t dataLen = totalLength_ - crcLength_;
      uint16_t calculated = utils::calculateCRC16Modbus(reinterpret_cast<const uint8_t*>(data), dataLen);

      const uint8_t* udata = reinterpret_cast<const uint8_t*>(data);
      // CRC16 Modbus is usually Little Endian (or implementation specific, assuming Little Endian per Utils)
      size_t crcOffset = totalLength_ - 2;
      uint16_t received = udata[crcOffset] | (udata[crcOffset + 1] << 8);

      if (calculated != received) {
        throw std::runtime_error("[EasyByteParserCpp]: CRC Check Failed: calculated=" + std::to_string(calculated) +
                                 ", received=" + std::to_string(received));
      }
    } else {
      throw std::runtime_error("[EasyByteParserCpp]: Unsupported CRC Algorithm: " + crcAlgo_);
    }
  }

  std::map<std::string, ParsedValue> result;

  for (const auto& field : fields_) {
    // Offset checks already done in validateConfig, but safe to check again or trust
    const char* ptr = data + field.byteOffset;
    ParsedValue val;

    if (field.type == "float") {
      auto raw = utils::readFromBuffer<float>(ptr, field.isBigEndian);
      val = ParsedValue(static_cast<double>(raw) * field.scale + field.bias);
    } else if (field.type == "bool") {
      auto raw = utils::readFromBuffer<uint8_t>(ptr, field.isBigEndian);
      if (field.bitCount > 0) raw = (raw >> field.bitOffset) & 1;
      val = ParsedValue(static_cast<bool>(raw));
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

      if (field.bitCount > 0) {
        if (isSigned) uVal = static_cast<uint64_t>(iVal);  // treat as bits
        uVal = (uVal >> field.bitOffset) & ((1ULL << field.bitCount) - 1);
        isSigned = false;  // Result of bitfield extraction is usually treated as unsigned
      }

      if (field.scale != 1.0 || field.bias != 0.0) {
        double d = isSigned ? static_cast<double>(iVal) : static_cast<double>(uVal);
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

std::string ByteParser::dumpRaw(const std::map<std::string, ParsedValue>& data) {
  std::stringstream ss;
  ss << "Data Dump:\n";
  for (const auto& [key, val] : data) {
    ss << key << " = " << val.toString() << "\n";
  }
  return ss.str();
}

std::string ByteParser::dumpJson(const std::map<std::string, ParsedValue>& data) {
  nlohmann::json j;
  // unflatten the keys "temp.engine_oil" -> {"temp": {"engine_oil":val}}

  for (const auto& [key, val] : data) {
    std::vector<std::string> parts = utils::split(key, '.');
    nlohmann::json* curr = &j;
    for (size_t i = 0; i < parts.size() - 1; ++i) {
      curr = &((*curr)[parts[i]]);
    }
    std::visit([&](auto&& arg) { (*curr)[parts.back()] = arg; }, val.getValue());
  }
  return j.dump(4);
}

std::string ByteParser::getConfigurationChecklist() const {
  std::stringstream ss;
  ss << "=== Parser Configuration Checklist ===\n";
  ss << "1. Total Length: " << totalLength_ << " bytes\n";

  ss << "2. Start Code:   ";
  if (startCode_.empty()) {
    ss << "None";
  } else {
    ss << "0x";
    for (auto b : startCode_) ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    ss << std::dec << " (Length: " << startCodeLength_ << ")";
  }
  ss << "\n";

  ss << "3. CRC Config:   " << (crcAlgo_.empty() ? "None" : crcAlgo_ + " (Length: " + std::to_string(crcLength_) + ")")
     << "\n";

  ss << "4. Fields Layout (" << fields_.size() << " fields):\n";
  ss << std::setfill(' ');

  // Sort fields by offset for display
  auto sortedFields = fields_;
  std::sort(sortedFields.begin(), sortedFields.end(), [](const FieldDefinition& a, const FieldDefinition& b) {
    if (a.byteOffset != b.byteOffset) return a.byteOffset < b.byteOffset;
    return a.bitOffset < b.bitOffset;
  });

  for (const auto& f : sortedFields) {
    ss << "   - [Offset " << std::setw(3) << f.byteOffset << "]";
    if (f.bitCount > 0) {
      ss << " [Bits " << f.bitOffset << ":" << (f.bitOffset + f.bitCount - 1) << "]";
    }
    ss << " " << std::setw(20) << std::left << f.name << " Type: " << std::setw(8) << f.type;
    if (f.scale != 1.0 || f.bias != 0.0) {
      ss << " (Scale: " << f.scale << ", Bias: " << f.bias << ")";
    }
    ss << "\n";
  }
  ss << "======================================\n";
  return ss.str();
}

}  // namespace easy_byte_parser
