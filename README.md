# InfiniteFetch

A free, open-source file distribution tool that downloads files via seed files from multiple protocol sources, including HTTP, HTTPS, FTP, SQL, and DNS.

## Features

- **Multi-protocol download** — HTTP, HTTPS, FTP, DNS TXT records, and SQL/ODBC queries
- **Seed-driven** — XML seed files describe files, blocks, download sources, and options
- **Block-level integrity** — SHA256 hash verification at block, file, and seed levels
- **Flexible sources** — Each block can specify custom HTTP headers, status code handling, timeouts, and retry behavior
- **File assembly** — Downloads blocks independently and assembles them at correct byte offsets
- **Metadata restoration** — Restores original file creation and modification timestamps from RFC 3339 dates
- **Cross-platform build** — CMake with automatic dependency fetching via FetchContent

## Requirements

- **CMake** 3.20 or later
- **C++20** compiler (MSVC 2022, GCC 11+, Clang 14+)
- **Windows** (uses WinHTTP/WinINet/BCrypt/WinDNS for native SSL and protocol support)
- Git (for FetchContent dependency downloads during build)

## Build

```bash
cmake -B build
cmake --build build --config Release
```

The resulting binary is `build/Release/infetch.exe`.

### Dependencies (auto-fetched by CMake)

| Library | Purpose |
|---------|---------|
| [pugixml](https://github.com/zeux/pugixml) v1.15 | XML seed file parsing |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) v0.20.0 | HTTP/HTTPS client |

## Usage

```bash
infetch [options] <seed_file> [output_dir]
```

| Option | Description |
|--------|-------------|
| `-v`, `--verbose` | Enable verbose output (shows block hashes, uploader, source count) |
| `seed_file` | Path to the XML seed file (required) |
| `output_dir` | Directory to save downloaded files (default: current directory) |

### Example

```bash
infetch seed_example.xml ./downloads
infetch -v my_files.xml
```

## Seed File Format

Seed files are XML documents that describe files, their blocks, and download sources.

### Root Element

```xml
<infetch hash="SHA256_of_seed_file">
```

| Attribute | Description |
|-----------|-------------|
| `hash` | SHA256 of the entire seed file (used for integrity verification) |

### Metadata

```xml
<title>My Files</title>
<charset>utf-8</charset>
```

### File Element

```xml
<file name="output_filename">
    <hash>SHA256_of_complete_file</hash>
    <size>total_bytes</size>
    <create_time>2024-01-15T14:30:00Z</create_time>
    <edit_time>2024-01-15T14:30:00+08:00</edit_time>
    <blocks>...</blocks>
</file>
```

Timestamps use RFC 3339 format with optional timezone offset.

### Block Element

```xml
<block id="0" range="0-1023" hash="SHA256_of_this_block">
    <uploader>Source ID (optional)</uploader>
    <linktype>http</linktype>
    <link>https://example.com/path</link>
    <options>...</options>
</block>
```

| Attribute | Description |
|-----------|-------------|
| `id` | Block identifier (integer) |
| `range` | Byte range as `start-end` (inclusive) |
| `hash` | Expected SHA256 of the downloaded block data |

### Link Types

| Value | Protocol |
|-------|----------|
| `http` | HTTP download |
| `https` | HTTPS download (TLS via Windows Schannel) |
| `ftp` | FTP download |
| `dns` | DNS TXT record query (domain = link value) |
| `sql` | SQL/ODBC query (link = query, connection string in options) |

### Options

Control download behavior per block:

```xml
<options>
    <!-- Custom HTTP headers -->
    <option class="header" name="User-Agent">thief</option>
    <option class="header" name="Authorization">Bearer token</option>

    <!-- Status code handling -->
    <option class="status_code" name="500">return ERR;</option>
    <option class="status_code" name="204">return 0000h;</option>

    <!-- Connection tuning -->
    <option class="timeout" name="seconds">60</option>
    <option class="retry" name="count">3</option>

    <!-- SQL connection string -->
    <option class="sql" name="connection">Driver={SQL Server};Server=localhost;Database=mydb;</option>
</options>
```

| Option class | Purpose |
|-------------|---------|
| `header` | HTTP headers (name = header name, value = header value) |
| `status_code` | Override behavior for specific HTTP status codes |
| `timeout` | Connection and read timeout in seconds (default: 30) |
| `retry` | Number of retry attempts on failure (default: 0) |
| `sql` | ODBC connection string for SQL downloads |

**Status code values:**
- `return ERR;` — Treat this status code as a fatal error
- `return 0000h;` — Return zero-filled data of the expected block size

## Architecture

```
main.cpp
  ├── SeedParser (seed_parser.cpp)
  │     └── pugixml → SeedInfo { files, blocks, links, options }
  │
  ├── Downloader (downloader.cpp)
  │     ├── HTTP/HTTPS → cpp-httplib + Windows Schannel
  │     ├── FTP        → WinINet
  │     ├── DNS        → DnsQuery (TXT records)
  │     └── SQL        → ODBC (SQLDriverConnect + SQLExecDirect)
  │
  └── Assembler (assembler.cpp)
        ├── SHA256 verification (block → file → seed)
        ├── Block assembly at byte offsets
        └── File timestamp restoration
```

### Hash Verification Chain

1. **Block hash** — Each block's SHA256 is verified against the seed after download
2. **File hash** — The complete assembled file is read back and verified
3. **Seed hash** — The seed file itself is hashed and compared to the declared value

### Error Handling

- Each block can have multiple download sources (links); failures fall through to the next source
- HTTP 5xx errors trigger automatic retries when `retry` count is configured
- Hash mismatches stop the program with a descriptive error message
- Seed hash warnings do not prevent execution (non-fatal)

## License

This project is free and open-source software.
