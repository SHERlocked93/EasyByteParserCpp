# EasyByteParserCpp

EasyByteParserCpp is a lightweight C++17 library for parsing binary data based on INI configuration files. It is designed to be easy to use and extend.

## Features

- Configuration Flexibility: Support for both INI file loading and Programmatic (Fluid) API.
- Type Support: `uint8`, `int8`, `uint16`, `int16`, `uint32`, `int32`, `float`, `bool`.
- Bit Fields: Direct support for extracting bit-packed fields with `BitOffset` and `BitCount`.
- Endianness: Support for Big-Endian and Little-Endian.
- Scaling & Bias: Automatic scaling (`y = x * scale + bias`) for raw values.
- Validation: Strict validation for Overlaps (Byte & Bit level), Bounds, and Types.
- Visual Checklist: Generate readable layout reports for verification.
- Modern C++: Uses C++17 features (`std::variant`, `std::map`).
- Dependencies: Uses `mINI@0.9.18` for INI parsing and `nlohmann::json@3.12.0` for JSON output (both bundled).

## Usage

### 1. Define Config

#### Option A: INI File (`config.ini`)

```ini
[Header]
; Mandatory
TotalLength=20

; Optional fields (Must appear in pairs if used)
StartCode=0203        ; Defines value to check
StartCodeLength=2     ; Defines length
CRCAlgo=CRC16         ; CRC Algorithm identifier
CRCLength=2           ; Length of CRC field

[MyFloat]
ByteOffset=4
Type=float
Endian=Big
Scale=0.1

[MyFlags]
ByteOffset=8
Type=uint8
BitOffset=0
BitCount=3
```

#### Option B: Programmatic API (C++)

```cpp
#include <EasyByteParserCpp/ByteParser.hpp>

// Create and configure
easy_byte_parser::ByteParser parser;

// Set Global Headers
parser.setTotalLength(20)
      .setStartCode({0x02, 0x03})
      .setCRC("CRC16", 2);

// Add Fields
// Method 1: Using Template Helper
// Syntax: addField<Type>(Name, ByteOffset, BitOff=0, BitCnt=0, BigEndian=true, Scale=1, Bias=0)

// Integers (8, 16, 32 bit, signed/unsigned)
      .addField<uint8_t>("MyUint8", 0);
      .addField<int16_t>("MyInt16", 1, 0, 0, false); // Little Endian
      .addField<uint32_t>("MyUint32", 3);            // Big Endian (default)

      // Floating Point
      .addField<float>("MyFloat", 7, 0, 0, true, 0.1, 1.5); // Scale=0.1, Bias=1.5

      // Boolean (Bit Field)
      .addField<bool>("MyBool", 11, 0, 1); // Byte 11, Bit 0

// Method 2: Using FieldDefinition Struct (Recommended for complex configs)
easy_byte_parser::FieldDefinition flags;
flags.name = "MyFlags";
flags.byteOffset = 8;
flags.type = "uint8";
flags.bitOffset = 0;
flags.bitCount = 3;
parser.addField(flags);

// Validate the configuration (checks for overlaps, types, etc.)
parser.validateConfig();

// Optional: Print a visual layout of your config
std::cout << parser.getConfigurationChecklist() << std::endl;
```

### 2. Parse

```cpp
#include <EasyByteParserCpp/ByteParser.hpp>

using namespace easy_byte_parser;

int main() {
    ByteParser parser;

    // METHOD 1: Load from INI
    parser.loadConfig("config.ini");

    // METHOD 2: Programmatic (as shown above)
    // parser.setTotalLength(20)...

    std::vector<char> buffer = { ... }; // Your binary data
    auto result = parser.parse(buffer);

    // Access via map
    double val = std::get<double>(result["MyFloat"].getValue());

    // Or dump to JSON
    std::cout << ByteParser::dumpJson(result) << std::endl;
}
```

## Build

### Prerequisites

- CMake >= 3.14
- C++17 compliant compiler

### Build Library

```bash
mkdir build && cd build && \
cmake -DCMAKE_BUILD_TYPE=Release .. && \
make && \
sudo make install
```

### Build Tests

To build the tests, set `BUILD_TESTING=ON`:

```bash
mkdir build && cd build && \
cmake -DBUILD_TESTING=ON .. && \
make && \
ctest --verbose
```

## License

MIT License. See [LICENSE](LICENSE) file.
