# EasyByteParserCpp

EasyByteParserCpp is a lightweight, header-only(ish) C++17 library for parsing binary data based on INI configuration files. It is designed to be easy to use and extend.

## Features

- **INI Configuration**: Define packet structure using standard INI files.
- **Type Support**: `uint8`, `int8`, `uint16`, `int16`, `uint32`, `int32`, `float`, `bool`.
- **Bit Fields**: Direct support for extracting bit-packed fields with `BitOffset` and `BitCount`.
- **Endianness**: Support for Big-Endian and Little-Endian.
- **Scaling & Bias**: Automatic scaling (`y = x * scale + bias`) for raw values.
- **Validation**: Built-in config validation (oversize checks, invalid types).
- **Modern C++**: Uses C++17 features (`std::variant`, `std::map`).
- **Dependencies**: Uses `mINI@0.9.18` for INI parsing and `nlohmann::json@3.12.0` for JSON output (both bundled).

## Usage

### 1. Define Config (`config.ini`)

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

### 2. Parse

```cpp
#include <EasyByteParserCpp/ByteParser.hpp>

using namespace easy_byte_parser;

int main() {
    ByteParser parser;
    parser.loadConfig("config.ini");

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
ctest
```

## License

MIT License. See [LICENSE](LICENSE) file.
