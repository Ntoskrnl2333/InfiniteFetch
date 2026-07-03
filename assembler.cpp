#include "assembler.h"
#include "sha256.h"

#include <fstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>

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
}
