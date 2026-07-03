#include "assembler.h"
#include "sha256.h"

#include <fstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Parse RFC3339 datetime string to FILETIME (UTC)
// Format: "2024-01-15T14:30:00Z" or "2024-01-15T14:30:00+08:00"
static bool parse_rfc3339(const std::string& dt, FILETIME& ft) {
    if (dt.empty()) return false;

    std::tm tm = {};
    int tz_hour = 0, tz_min = 0;
    char tz_sign = '+';

    std::istringstream ss(dt);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        // Try without seconds
        ss.clear();
        ss.seekg(0);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M");
        if (ss.fail()) return false;
    }

    // Parse timezone offset if present
    if (ss.peek() == 'Z' || ss.peek() == 'z') {
        // UTC
    } else if (ss.peek() == '+' || ss.peek() == '-') {
        ss >> tz_sign;
        int tz_offset;
        if (ss >> tz_offset) {
            tz_hour = tz_offset / 100;
            tz_min = tz_offset % 100;
        }
    }

    // Convert tm to UTC time_t
    std::time_t t = _mkgmtime64(&tm);
    if (t == -1) return false;

    // Apply timezone offset to get UTC
    if (tz_sign == '+') {
        t -= tz_hour * 3600 + tz_min * 60;
    } else if (tz_sign == '-') {
        t += tz_hour * 3600 + tz_min * 60;
    }

    LONGLONG ll = Int32x32To64(static_cast<LONG>(t), 10000000) + 116444736000000000LL;
    ft.dwLowDateTime = static_cast<DWORD>(ll);
    ft.dwHighDateTime = static_cast<DWORD>(ll >> 32);
    return true;
}

// Apply file timestamps from seed metadata
static void apply_file_times(const std::string& filepath,
                              const std::string& create_time,
                              const std::string& edit_time) {
    FILETIME ft_create, ft_edit;
    bool has_create = parse_rfc3339(create_time, ft_create);
    bool has_edit = parse_rfc3339(edit_time, ft_edit);

    if (!has_create && !has_edit) return;

    HANDLE hFile = CreateFileA(filepath.c_str(),
                                FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Warning: Failed to open file for timestamp update: " << filepath << std::endl;
        return;
    }

    SetFileTime(hFile,
                has_create ? &ft_create : nullptr,
                nullptr, // last access time unchanged
                has_edit ? &ft_edit : nullptr);

    CloseHandle(hFile);
    std::cout << "Applied file timestamps to " << filepath << std::endl;
}

void assemble_file(const FileInfo& file_info,
                   const std::vector<BlockData>& blocks,
                   const std::string& output_dir) {

    std::string output_path = output_dir + "/" + file_info.name;

    // Verify each block's hash before writing
    for (const auto& bd : blocks) {
        // Find the matching block info
        auto it = std::find_if(file_info.blocks.begin(), file_info.blocks.end(),
            [&](const BlockInfo& bi) { return bi.id == bd.block_id; });

        if (it == file_info.blocks.end()) {
            throw std::runtime_error("Block " + std::to_string(bd.block_id) +
                                     " not found in seed info for file " + file_info.name);
        }

        const auto& bi = *it;
        if (!bi.hash.empty()) {
            std::string actual_hash = sha256_hex(bd.data);
            if (actual_hash != bi.hash) {
                throw std::runtime_error(
                    "Hash mismatch for block " + std::to_string(bd.block_id) +
                    " of file " + file_info.name +
                    "\n  Expected: " + bi.hash +
                    "\n  Got:      " + actual_hash);
            }
            std::cout << "Block " << bd.block_id << " hash verified OK" << std::endl;
        } else {
            std::cout << "Block " << bd.block_id << " has no hash, skipping verification" << std::endl;
        }
    }

    // Determine total file size for pre-allocation
    int64_t total_size = 0;
    for (const auto& bi : file_info.blocks) {
        int64_t block_size = bi.range_end - bi.range_start + 1;
        if (bi.range_end + 1 > total_size) {
            total_size = bi.range_end + 1;
        }
    }

    // Write blocks to file at their correct offsets
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Failed to create output file: " + output_path);
    }

    // Pre-allocate file size
    out.seekp(static_cast<std::streamoff>(total_size - 1));
    out.write("", 1);

    for (const auto& bd : blocks) {
        auto it = std::find_if(file_info.blocks.begin(), file_info.blocks.end(),
            [&](const BlockInfo& bi) { return bi.id == bd.block_id; });

        const auto& bi = *it;
        out.seekp(static_cast<std::streamoff>(bi.range_start));
        out.write(reinterpret_cast<const char*>(bd.data.data()), bd.data.size());

        std::cout << "Wrote block " << bd.block_id << " at offset "
                  << bi.range_start << " (" << bd.data.size() << " bytes)" << std::endl;
    }

    out.close();

    // Verify final file hash
    if (!file_info.hash.empty()) {
        // Read back the file
        std::ifstream in(output_path, std::ios::binary | std::ios::ate);
        if (!in) {
            throw std::runtime_error("Failed to open output file for hash verification");
        }
        size_t file_size = static_cast<size_t>(in.tellg());
        in.seekg(0);
        std::vector<uint8_t> file_data(file_size);
        in.read(reinterpret_cast<char*>(file_data.data()), file_size);
        in.close();

        std::string actual_hash = sha256_hex(file_data);
        if (actual_hash != file_info.hash) {
            throw std::runtime_error(
                "Hash mismatch for file " + file_info.name +
                "\n  Expected: " + file_info.hash +
                "\n  Got:      " + actual_hash);
        }
        std::cout << "File " << file_info.name << " hash verified OK" << std::endl;
    }

    std::cout << "Successfully assembled: " << output_path << std::endl;

    // Apply original timestamps from seed metadata
    apply_file_times(output_path, file_info.create_time, file_info.edit_time);
}
