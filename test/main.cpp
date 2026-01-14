#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "EasyByteParserCpp/ByteParser.hpp"

using namespace easy_byte_parser;

// Helper CRC for test (Modbus)
uint16_t calcCRC(const std::vector<char> &data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint8_t)data[i];
    for (int j = 0; j < 8; ++j) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  return crc;
}

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
  // ByteOffset 0,1: StartCode=0203
  buffer[0] = 0x02;
  buffer[1] = 0x03;

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

  // Calculate CRC (Total 20, CRC 2 => Data 18)
  uint16_t crc = calcCRC(buffer, 18);
  buffer[18] = crc & 0xFF;
  buffer[19] = (crc >> 8) & 0xFF;

  auto result = parser.parse(buffer);

  // Test Invalid CRC
  std::cout << "Testing Invalid CRC..." << std::endl;
  auto bad_buffer = buffer;
  bad_buffer[18] ^= 0xFF;  // Corrupt CRC
  bool caught = false;
  try {
    parser.parse(bad_buffer);
  } catch (const std::exception &e) {
    std::cout << "Caught expected CRC error: " << e.what() << std::endl;
    caught = true;
  }
  if (!caught) {
    std::cerr << "Failed to catch invalid CRC!" << std::endl;
    std::exit(1);
  }

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
    std::cerr << "FAILED: Did not catch expected 'exceeds TotalLength' exception." << std::endl;
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
    if (msg.find("Invalid Type") != std::string::npos) {
      caught = true;
    }
  }

  if (!caught) {
    std::cerr << "FAILED: Did not catch expected 'Invalid Type' exception." << std::endl;
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
    std::cerr << "StartCodeLength expected 2, got " << parser.getStartCodeLength() << std::endl;
    std::exit(1);
  }
  if (!parser.getStartCode().empty()) {
    std::cerr << "StartCode expected empty" << std::endl;
    std::exit(1);
  }
  if (parser.getCRCLength() != 2) {
    std::cerr << "CRCLength expected 2, got " << parser.getCRCLength() << std::endl;
    std::exit(1);
  }
  if (!parser.getCRCAlgo().empty()) {
    std::cerr << "CRCAlgo expected empty, got " << parser.getCRCAlgo() << std::endl;
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

void check_failure(const std::string &configInfo, const std::string &expectedMsg) {
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
  std::cerr << "FAILED: Did not catch expected message '" << expectedMsg << "'" << std::endl;
  std::exit(1);
}

void test_overlap_checks() {
  check_failure("test_config_overlap_field.ini", "Overlap detected");
  check_failure("test_config_overlap_crc.ini", "overlaps with CRC");
  check_failure("test_config_overlap_bits.ini", "Overlap detected");

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

void test_programmatic_api() {
  std::cout << "Running test_programmatic_api..." << std::endl;
  ByteParser parser;

  // Fluent API Test
  parser.setTotalLength(20)
      .setStartCode({0x02, 0x03}, 2)
      .setCRC("CRC16", 2)
      .addField<uint8_t>("MyUint8", 2)
      .addField<float>("MyFloat", 4, 0, 0, true, 2.0, 1.5);  // Big Endian, Scale 2.0, Bias 1.5

  // Print Visual Checklist
  std::cout << parser.getConfigurationChecklist() << std::endl;

  // Validate Config (Implicitly called by parse, but here explicit)
  try {
    parser.validateConfig();
  } catch (const std::exception &e) {
    std::cerr << "Programmatic API validation failed: " << e.what() << std::endl;
    std::exit(1);
  }

  // Prepare Data
  std::vector<char> buffer(20, 0);
  // StartCode
  buffer[0] = 0x02;
  buffer[1] = 0x03;
  // MyUint8
  buffer[2] = 100;
  // MyFloat = 1.0 (0x3F800000 Big Endian) -> Scaled: 1.0 * 2.0 + 1.5 = 3.5
  uint32_t f_int = 0x3F800000;
  buffer[4] = (f_int >> 24) & 0xFF;
  buffer[5] = (f_int >> 16) & 0xFF;
  buffer[6] = (f_int >> 8) & 0xFF;
  buffer[7] = (f_int)&0xFF;

  // Calculate CRC for Programmatic Test
  uint16_t crc = calcCRC(buffer, 18);
  buffer[18] = crc & 0xFF;
  buffer[19] = (crc >> 8) & 0xFF;

  auto result = parser.parse(buffer);

  if (std::get<uint64_t>(result["MyUint8"].getValue()) != 100) {
    std::cerr << "Programmatic MyUint8 failed" << std::endl;
    std::exit(1);
  }
  if (std::abs(std::get<double>(result["MyFloat"].getValue()) - 3.5) > 0.0001) {
    double val = std::get<double>(result["MyFloat"].getValue());
    std::cerr << "Programmatic MyFloat failed: " << val << " expected 3.5" << std::endl;
    std::exit(1);
  }

  std::cout << "test_programmatic_api PASSED" << std::endl;
}

void test_programmatic_comprehensive() {
  std::cout << "Running test_programmatic_comprehensive..." << std::endl;

  // 1. Data Types Coverage
  {
    std::cout << "  [1] Testing All Data Types..." << std::endl;
    ByteParser parser;
    parser.setTotalLength(10);

    // Layout:
    // 0: uint8  (val 255)
    // 1: int8   (val -5)
    // 2-3: uint16 (val 0x1234)
    // 4-7: float (val 3.14)
    // 8: bool (val true)

    parser.addField<uint8_t>("u8", 0)
        .addField<int8_t>("i8", 1)
        .addField<uint16_t>("u16", 2)  // Big Endian default
        .addField<float>("f", 4)
        .addField<bool>("b", 8);

    parser.validateConfig();

    std::vector<char> buf(10, 0);
    buf[0] = (char)0xFF;
    buf[1] = (char)-5;
    // u16 = 0x1234
    buf[2] = 0x12;
    buf[3] = 0x34;
    // float 3.14 = 0x4048F5C3 (approx)
    uint32_t fraw = 0x4048F5C3;
    buf[4] = (fraw >> 24) & 0xFF;
    buf[5] = (fraw >> 16) & 0xFF;
    buf[6] = (fraw >> 8) & 0xFF;
    buf[7] = fraw & 0xFF;
    buf[8] = 1;

    auto res = parser.parse(buf);

    if (std::get<uint64_t>(res["u8"].getValue()) != 255) throw std::runtime_error("u8 failed");
    if (std::get<int64_t>(res["i8"].getValue()) != -5) throw std::runtime_error("i8 failed");
    if (std::get<uint64_t>(res["u16"].getValue()) != 0x1234) throw std::runtime_error("u16 failed");
    if (std::abs(std::get<double>(res["f"].getValue()) - 3.14) > 0.001) throw std::runtime_error("float failed");
    if (std::get<bool>(res["b"].getValue()) != true) throw std::runtime_error("bool failed");

    std::cout << "      -> All types passed." << std::endl;
  }

  // 2. StartCode (Presence/Absence/Error)
  {
    std::cout << "  [2] Testing StartCode..." << std::endl;
    // Case A: No StartCode
    ByteParser pNoSC;
    pNoSC.setTotalLength(1).addField<uint8_t>("v", 0);
    std::vector<char> b1(1, 10);
    pNoSC.parse(b1);  // Should not throw

    // Case B: With StartCode - Valid
    ByteParser pSC;
    pSC.setTotalLength(3).setStartCode({0xAA, 0xBB}, 2).addField<uint8_t>("v", 2);
    std::vector<char> b2 = {(char)0xAA, (char)0xBB, 10};
    pSC.parse(b2);  // Should pass

    // Case C: With StartCode - Invalid
    std::vector<char> b3 = {(char)0xAA, (char)0xCC, 10};  // Wrong 2nd byte
    bool caught = false;
    try {
      pSC.parse(b3);
    } catch (const std::exception &e) {
      if (std::string(e.what()).find("Invalid Start Code") != std::string::npos) caught = true;
    }
    if (!caught) {
      std::cerr << "Failed to catch invalid start code" << std::endl;
      std::exit(1);
    }
    std::cout << "      -> StartCode checks passed." << std::endl;
  }

  // 3. CRC (Presence/Absence/Error)
  {
    std::cout << "  [3] Testing CRC..." << std::endl;
    ByteParser pCRC;
    pCRC.setTotalLength(4).setCRC("CRC16", 2).addField<uint16_t>("val", 0);  // Data 0-1, CRC 2-3

    std::vector<char> buf(4);
    buf[0] = 0x01;
    buf[1] = 0x02;  // Data = 0x0102

    // Calculate CRC for 0x01, 0x02
    uint16_t expected = calcCRC(buf, 2);
    // Little Endian CRC in buffer (Low Byte, High Byte)
    buf[2] = expected & 0xFF;
    buf[3] = (expected >> 8) & 0xFF;

    // Valid
    pCRC.parse(buf);

    // Invalid
    buf[2] ^= 0xFF;  // Corrupt
    bool caught = false;
    try {
      pCRC.parse(buf);
    } catch (const std::exception &e) {
      if (std::string(e.what()).find("CRC Check Failed") != std::string::npos) caught = true;
    }
    if (!caught) {
      std::cerr << "Failed to catch invalid CRC" << std::endl;
      std::exit(1);
    }
    std::cout << "      -> CRC checks passed." << std::endl;
  }

  // 4. Boundaries correctness & violation
  {
    std::cout << "  [4] Testing Boundaries..." << std::endl;
    ByteParser p;
    p.setTotalLength(5);
    p.addField<uint8_t>("v", 0);

    // Case A: Buffer too small
    std::vector<char> smallBuf(4);
    bool caught = false;
    try {
      p.parse(smallBuf);
    } catch (const std::exception &e) {
      if (std::string(e.what()).find("Buffer size") != std::string::npos) caught = true;
    }
    if (!caught) {
      std::cerr << "Failed to catch small buffer" << std::endl;
      std::exit(1);
    }

    // Case B: Config Definition Out of Bounds
    ByteParser pBad;
    pBad.setTotalLength(5);
    // Field at offset 4, size 2 (needs 4,5 -> 6 bytes total, limit is 5)
    pBad.addField<uint16_t>("bad", 4);

    caught = false;
    try {
      pBad.validateConfig();
    } catch (const std::exception &e) {
      if (std::string(e.what()).find("exceeds TotalLength") != std::string::npos) caught = true;
    }
    if (!caught) {
      std::cerr << "Failed to catch config boundary violation" << std::endl;
      std::exit(1);
    }
    std::cout << "      -> Boundary checks passed." << std::endl;
  }

  std::cout << "test_programmatic_comprehensive PASSED" << std::endl;
}

void test_programmatic_ini_equivalents() {
  std::cout << "Running test_programmatic_ini_equivalents..." << std::endl;

  // 1. test_config.ini (Valid)
  // [Header] StartCode=0203, TotalLength=20, CRCAlgo=CRC16, CRCLength=2
  {
    std::cout << "  (1) Equivalent to test_config.ini" << std::endl;
    ByteParser p;
    p.setTotalLength(20)
        .setStartCode({0x02, 0x03}, 2)
        .setCRC("CRC16", 2)
        // [test.uint8_val] ByteOffset=2 Type=uint8 Endian=big
        .addField<uint8_t>("test.uint8_val", 2)
        // [test.uint16_big] ByteOffset=3 Type=uint16 Endian=big
        .addField<uint16_t>("test.uint16_big", 3)
        // [test.uint16_little] ByteOffset=5 Type=uint16 Endian=little
        .addField<uint16_t>("test.uint16_little", 5, 0, 0, false)
        // [test.float_val] ByteOffset=7 Type=float Scale=2.0 Bias=1.5
        .addField<float>("test.float_val", 7, 0, 0, true, 2.0, 1.5)
        // [bit.flag1] ByteOffset=11 Type=uint8 BitOffset=0 BitCount=1
        .addField<uint8_t>("bit.flag1", 11, 0, 1)
        // [bit.mode] ByteOffset=11 Type=uint8 BitOffset=1 BitCount=3
        .addField<uint8_t>("bit.mode", 11, 1, 3);

    p.validateConfig();  // Should produce no error

    // Create a dummy valid buffer
    std::vector<char> buf(20, 0);
    buf[0] = 0x02;
    buf[1] = 0x03;  // SC
    buf[2] = 10;    // uint8
    buf[3] = 0x12;
    buf[4] = 0x34;  // u16 big
    buf[5] = (char)0xCD;
    buf[6] = (char)0xAB;          // u16 little 0xABCD
    uint32_t f_int = 0x3F800000;  // 1.0
    buf[7] = (f_int >> 24) & 0xFF;
    buf[8] = (f_int >> 16) & 0xFF;
    buf[9] = (f_int >> 8) & 0xFF;
    buf[10] = f_int & 0xFF;
    // Bits: flag1=1, mode=5(101) -> 1011 = 0x0B
    buf[11] = 0x0B;

    uint16_t crc = calcCRC(buf, 18);
    buf[18] = crc & 0xFF;
    buf[19] = (crc >> 8) & 0xFF;

    auto res = p.parse(buf);
    std::cout << "      JSON Output: " << ByteParser::dumpJson(res) << std::endl;
  }

  // 2. test_config_optional.ini (Valid, minimal)
  // [Header] StartCode=, TotalLength=20, CRCAlgo=, CRCLength=2
  // [test.val] ByteOffset=2 Type=uint8
  {
    std::cout << "  (2) Equivalent to test_config_optional.ini" << std::endl;
    ByteParser p;
    // Note: mirror loadConfig behavior where missing parts are skipped/defaults
    p.setTotalLength(20);
    p.setStartCode({}, 2);
    p.setCRC("", 2);

    p.addField<uint8_t>("test.val", 2);

    p.validateConfig();

    std::vector<char> buf(20, 0);
    buf[2] = 123;
    auto res = p.parse(buf);
    std::cout << "      JSON Output: " << ByteParser::dumpJson(res) << std::endl;
  }

  // 3. test_valid_bits.ini (Valid)
  // [Header] TotalLength=20...
  // [bits1] Off=2 BitOff=0 BitCount=4
  // [bits2] Off=2 BitOff=4 BitCount=4
  {
    std::cout << "  (3) Equivalent to test_config_valid_bits.ini" << std::endl;
    ByteParser p;
    p.setTotalLength(20).addField<uint8_t>("bits1", 2, 0, 4).addField<uint8_t>("bits2", 2, 4, 4);

    p.validateConfig();

    std::vector<char> buf(20, 0);
    buf[2] = 0xAB;  // 1010 1011. bits1 (low 4) = B(11), bits2 (high 4) = A(10)

    auto res = p.parse(buf);
    std::cout << "      JSON Output: " << ByteParser::dumpJson(res) << std::endl;
  }

  // 4. test_config_bad_bit.ini (Invalid)
  // [bad.bit] Off=0 BitOff=5 BitCount=4 -> 5+4=9 > 8. Fail.
  {
    std::cout << "  (4) Equivalent to test_config_bad_bit.ini (Expect Failure)" << std::endl;
    ByteParser p;
    p.setTotalLength(10);

    p.addField<uint8_t>("bad.bit", 0, 5, 4);

    bool caught = false;
    try {
      p.validateConfig();
    } catch (const std::exception &e) {
      if (std::string(e.what()).find("Bit logic exceeds type width") != std::string::npos)
        caught = true;
      else
        std::cout << "Wrong error: " << e.what() << std::endl;
    }
    if (!caught) {
      std::cerr << "Failed to catch bad_bit error" << std::endl;
      std::exit(1);
    }
    std::cout << "      -> Caught expected error." << std::endl;
  }

  // 5. test_config_bad_type.ini (Invalid)
  // Type=uint128 -> Invalid Type.
  {
    std::cout << "  (5) Equivalent to test_config_bad_type.ini (Expect Failure)" << std::endl;
    ByteParser p;
    p.setTotalLength(10);

    bool caught = false;
    try {
      FieldDefinition fd;
      fd.name = "bad.type";
      fd.byteOffset = 0;
      fd.type = "uint128";
      p.addField(fd);
    } catch (const std::exception &e) {
      if (std::string(e.what()).find("Invalid type") != std::string::npos)
        caught = true;
      else
        std::cout << "Wrong error: " << e.what() << std::endl;
    }
    if (!caught) {
      std::cerr << "Failed to catch bad_type error" << std::endl;
      std::exit(1);
    }
    std::cout << "      -> Caught expected error." << std::endl;
  }

  // 6. test_config_invalid.ini (Invalid)
  // [invalid.oversize] Off=8 Type=uint32 (4 bytes). 8+4=12 > 10.
  {
    std::cout << "  (6) Equivalent to test_config_invalid.ini (Expect Failure)" << std::endl;
    ByteParser p;
    p.setTotalLength(10);

    p.addField<uint32_t>("invalid.oversize", 8);

    bool caught = false;
    try {
      p.validateConfig();
    } catch (const std::exception &e) {
      if (std::string(e.what()).find("exceeds TotalLength") != std::string::npos)
        caught = true;
      else
        std::cout << "Wrong error: " << e.what() << std::endl;
    }
    if (!caught) {
      std::cerr << "Failed to catch oversize error" << std::endl;
      std::exit(1);
    }
    std::cout << "      -> Caught expected error." << std::endl;
  }

  // 7. test_config_overlap_field.ini (Invalid)
  // field1 @ 2, field2 @ 2.
  {
    std::cout << "  (7) Equivalent to test_config_overlap_field.ini (Expect Failure)" << std::endl;
    ByteParser p;
    p.setTotalLength(20).addField<uint8_t>("field1", 2).addField<uint8_t>("field2", 2);

    bool caught = false;
    try {
      p.validateConfig();
    } catch (const std::exception &e) {
      if (std::string(e.what()).find("Overlap detected") != std::string::npos)
        caught = true;
      else
        std::cout << "Wrong error: " << e.what() << std::endl;
    }
    if (!caught) {
      std::cerr << "Failed to catch overlap field error" << std::endl;
      std::exit(1);
    }
    std::cout << "      -> Caught expected error." << std::endl;
  }

  // 8. test_config_overlap_crc.ini (Invalid)
  // field @ 18, size 2. CRC @ 18, size 2.
  {
    std::cout << "  (8) Equivalent to test_config_overlap_crc.ini (Expect Failure)" << std::endl;
    ByteParser p;
    p.setTotalLength(20).setCRC("CRC16", 2).addField<uint16_t>("field_in_crc", 18);

    bool caught = false;
    try {
      p.validateConfig();
    } catch (const std::exception &e) {
      if (std::string(e.what()).find("overlaps with CRC") != std::string::npos)
        caught = true;
      else
        std::cout << "Wrong error: " << e.what() << std::endl;
    }
    if (!caught) {
      std::cerr << "Failed to catch overlap CRC error" << std::endl;
      std::exit(1);
    }
    std::cout << "      -> Caught expected error." << std::endl;
  }

  // 9. test_config_overlap_bits.ini (Invalid)
  // bits3_overlap @ 2, bitOffset 2, count 4. bits1(0,4), bits2(4,4).
  {
    std::cout << "  (9) Equivalent to test_config_overlap_bits.ini (Expect Failure)" << std::endl;
    ByteParser p;
    p.setTotalLength(20)
        .addField<uint8_t>("bits1", 2, 0, 4)
        .addField<uint8_t>("bits2", 2, 4, 4)
        .addField<uint8_t>("bits3_overlap", 2, 2, 4);

    bool caught = false;
    try {
      p.validateConfig();
    } catch (const std::exception &e) {
      if (std::string(e.what()).find("Overlap detected") != std::string::npos)
        caught = true;
      else
        std::cout << "Wrong error: " << e.what() << std::endl;
    }
    if (!caught) {
      std::cerr << "Failed to catch overlap bits error" << std::endl;
      std::exit(1);
    }
    std::cout << "      -> Caught expected error." << std::endl;
  }

  // Bonus: Print checklist for valid ones
  std::cout << "  [Checklist for valid_bits]" << std::endl;
  ByteParser p;
  p.setTotalLength(20).addField<uint8_t>("bits1", 2, 0, 4).addField<uint8_t>("bits2", 2, 4, 4);
  std::cout << p.getConfigurationChecklist() << std::endl;
}

int main() {
  test_parsing();
  test_threads();
  test_invalid_config();
  test_bad_type_config();
  test_bad_bit_config();
  test_optional_header();
  test_overlap_checks();
  test_programmatic_api();
  test_programmatic_comprehensive();
  test_programmatic_ini_equivalents();
  return 0;
}
