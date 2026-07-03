#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include "infetch.h"
#include <vector>
#include <cstdint>
#include <string>

struct BlockData {
    int block_id;
    std::vector<uint8_t> data;
};

// Assemble downloaded blocks into the output file.
// Verifies each block's SHA256 hash and the final file hash.
// Throws on hash mismatch.
void assemble_file(const FileInfo& file_info,
                   const std::vector<BlockData>& blocks,
                   const std::string& output_dir);

#endif // ASSEMBLER_H
