#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "EasyByteParserCpp/ByteParser.hpp"

using namespace easy_byte_parser;

void test_parsing() {
  std::cout << "Running test_parsing..." << std::endl;
  ByteParser parser;

  try {
    parser.loadConfig("test_config.ini");
  } catch (const std::exception &e) {
    std::cerr << "Failed to load config: " << e.what() << std::endl;
    std::exit(1);
  }

  // Prepare buffer (TotalLength=20)
  std::vector<char> buffer(20, 0);

  // Fill data
  // ByteOffset 2: uint8 = 10
  buffer[2] = 10;

  // ByteOffset 3: uint16 big endian = 0x1234
  buffer[3] = 0x12;
  buffer[4] = 0x34;

  // ByteOffset 5: uint16 little endian = 0xABCD
  buffer[5] = (char)0xCD;
  buffer[6] = (char)0xAB;

  // ByteOffset 7: float = 1.0 -> scaled * 2.0 + 1.5 = 3.5
  // IEEE 754 float 1.0 is 0x3F800000 (Big Endian)
  uint32_t f_int = 0x3F800000;
  buffer[7] = (f_int >> 24) & 0xFF;
  buffer[8] = (f_int >> 16) & 0xFF;
  buffer[9] = (f_int >> 8) & 0xFF;
  buffer[10] = (f_int)&0xFF;

  // ByteOffset 11: Bits
  // Flag1 (bit 0) = 1
  // Mode (bit 1-3) = 5 (101 binary) -> shifted left 1 = 1010
  // Total = 1011 = 0x0B
  buffer[11] = 0x0B;

  auto result = parser.parse(buffer);

  // Verify
  // Note: getValue return variant. We need to handle type correctly.
  // uint8 -> uint64_t
  try {
    if (std::get<uint64_t>(result["test.uint8_val"].getValue()) != 10) {
      std::cerr << "uint8_val failed" << std::endl;
      std::exit(1);
    }

    if (std::get<uint64_t>(result["test.uint16_big"].getValue()) != 0x1234) {
      std::cerr << "uint16_big failed" << std::endl;
      std::exit(1);
    }

    if (std::get<uint64_t>(result["test.uint16_little"].getValue()) != 0xABCD) {
      std::cerr << "uint16_little failed" << std::endl;
      std::exit(1);
    }

    double fv = std::get<double>(result["test.float_val"].getValue());
    if (std::abs(fv - 3.5) > 0.0001) {
      std::cerr << "Float mismatch: " << fv << " expected 3.5" << std::endl;
      std::exit(1);
    }

    // Bit checks
    if (std::get<uint64_t>(result["bit.flag1"].getValue()) != 1) {
      std::cerr << "bit.flag1 failed" << std::endl;
      std::exit(1);
    }
    if (std::get<uint64_t>(result["bit.mode"].getValue()) != 5) {
      std::cerr << "bit.mode failed" << std::endl;
      std::exit(1);
    }

  } catch (const std::bad_variant_access &e) {
    std::cerr << "Variant access failed: " << e.what() << std::endl;
    std::exit(1);
  }

  std::cout << "Raw Dump:\n" << ByteParser::dumpRaw(result) << std::endl;
  std::cout << "JSON Dump:\n" << ByteParser::dumpJson(result) << std::endl;

  std::cout << "test_parsing PASSED" << std::endl;
}

void test_threads() {
  std::cout << "Running test_threads..." << std::endl;
  std::thread t([]() { std::cout << "Hello from thread" << std::endl; });
  t.join();
}

void test_invalid_config() {
  std::cout << "Running test_invalid_config..." << std::endl;
  ByteParser parser;
  bool caught = false;
  try {
    parser.loadConfig("test_config_invalid.ini");
  } catch (const std::exception &e) {
    std::cout << "Expected exception caught: " << e.what() << std::endl;
    std::string msg = e.what();
    if (msg.find("exceeds TotalLength") != std::string::npos) {
      caught = true;
    }
  }

  if (!caught) {
    std::cerr
        << "FAILED: Did not catch expected 'exceeds TotalLength' exception."
        << std::endl;
    std::exit(1);
  }
  std::cout << "test_invalid_config PASSED" << std::endl;
}

void test_bad_type_config() {
  std::cout << "Running test_bad_type_config..." << std::endl;
  ByteParser parser;
  bool caught = false;
  try {
    parser.loadConfig("test_config_bad_type.ini");
  } catch (const std::exception &e) {
    std::cout << "Expected exception caught: " << e.what() << std::endl;
    std::string msg = e.what();
    if (msg.find("invalid Type") != std::string::npos) {
      caught = true;
    }
  }

  if (!caught) {
    std::cerr << "FAILED: Did not catch expected 'invalid Type' exception."
              << std::endl;
    std::exit(1);
  }
  std::cout << "test_bad_type_config PASSED" << std::endl;
}

void test_bad_bit_config() {
  std::cout << "Running test_bad_bit_config..." << std::endl;
  ByteParser parser;
  bool caught = false;
  try {
    parser.loadConfig("test_config_bad_bit.ini");
  } catch (const std::exception &e) {
    std::cout << "Expected exception caught: " << e.what() << std::endl;
    std::string msg = e.what();
    if (msg.find("Bit logic exceeds type width") != std::string::npos) {
      caught = true;
    }
  }

  if (!caught) {
    std::cerr << "FAILED: Did not catch expected 'Bit logic exceeds type "
                 "width' exception."
              << std::endl;
    std::exit(1);
  }
  std::cout << "test_bad_bit_config PASSED" << std::endl;
}

void test_optional_header() {
  std::cout << "Running test_optional_header..." << std::endl;
  ByteParser parser;
  parser.loadConfig("test_config_optional.ini");

  if (parser.getStartCodeLength() != 2) {
    std::cerr << "StartCodeLength expected 2, got "
              << parser.getStartCodeLength() << std::endl;
    std::exit(1);
  }
  if (!parser.getStartCode().empty()) {
    std::cerr << "StartCode expected empty" << std::endl;
    std::exit(1);
  }
  if (parser.getCRCLength() != 2) {
    std::cerr << "CRCLength expected 2, got " << parser.getCRCLength()
              << std::endl;
    std::exit(1);
  }
  if (!parser.getCRCAlgo().empty()) {
    std::cerr << "CRCAlgo expected empty, got " << parser.getCRCAlgo()
              << std::endl;
    std::exit(1);
  }

  // Parse valid buffer
  std::vector<char> buffer(20, 0);
  // test.val is uint8 at offset 2.
  // Note: Offset 2 is after StartCode (2 bytes) technically?
  // The parser uses absolute byteOffset defined in config.
  // In test_parsing, StartCode=0203 (2 bytes), and test.uint8_val is
  // ByteOffset=2. So validation StartCode is 0..1. test.uint8_val is 2. Here
  // same logic locally.
  buffer[2] = 42;

  auto result = parser.parse(buffer);

  try {
    if (std::get<uint64_t>(result["test.val"].getValue()) != 42) {
      std::cerr << "test.val failed" << std::endl;
      std::exit(1);
    }
  } catch (...) {
    std::cerr << "test.val access failed" << std::endl;
    std::exit(1);
  }
  std::cout << "test_optional_header PASSED" << std::endl;
}

void check_failure(const std::string &configInfo,
                   const std::string &expectedMsg) {
  std::cout << "Running check_failure for " << configInfo << "..." << std::endl;
  ByteParser parser;
  try {
    parser.loadConfig(configInfo);
  } catch (const std::exception &e) {
    std::cout << "Expected exception caught: " << e.what() << std::endl;
    if (std::string(e.what()).find(expectedMsg) != std::string::npos) {
      std::cout << "PASSED" << std::endl;
      return;
    }
  }
  std::cerr << "FAILED: Did not catch expected message '" << expectedMsg << "'"
            << std::endl;
  std::exit(1);
}

void test_overlap_checks() {
  check_failure("test_config_overlap_field.ini", "overlaps with another field");
  check_failure("test_config_overlap_crc.ini", "overlaps with CRC tail region");
  check_failure("test_config_overlap_bits.ini", "overlaps with another field");

  // Valid bits overlap check
  std::cout << "Running test_valid_bits..." << std::endl;
  ByteParser parser;
  try {
    parser.loadConfig("test_config_valid_bits.ini");
  } catch (const std::exception &e) {
    std::cerr << "FAILED: Valid bits config threw: " << e.what() << std::endl;
    std::exit(1);
  }
  std::cout << "test_valid_bits PASSED" << std::endl;
}

int main() {
  test_parsing();
  test_threads();
  test_invalid_config();
  test_bad_type_config();
  test_bad_bit_config();
  test_optional_header();
  test_overlap_checks();
  return 0;
}
