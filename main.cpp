#include "infetch.h"
#include "vars.h"
#include "seed_parser.h"
#include "downloader.h"
#include "assembler.h"

#include <iostream>
#include <string>
#include <vector>

static void print_usage(const char* prog) {
    std::cout << "InfiniteFetch v" << VERSION << " (" << PLATFORM << ")\n"
              << "Usage: " << prog << " <seed_file> [output_dir]\n"
              << "  seed_file   Path to the XML seed file\n"
              << "  output_dir  Directory to save downloaded files (default: .)\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string seed_path = argv[1];
    std::string output_dir = (argc >= 3) ? argv[2] : ".";

    try {
        std::cout << "Parsing seed file: " << seed_path << std::endl;
        SeedInfo seed = parse_seed(seed_path);
        std::cout << "Seed: " << seed.title << " (charset: " << seed.charset << ")" << std::endl;

        // Verify seed file integrity
        if (!verify_seed_hash(seed_path, seed.hash)) {
            std::cerr << "Warning: Seed file hash verification failed, continuing anyway..." << std::endl;
        } else if (!seed.hash.empty()) {
            std::cout << "Seed hash verified OK" << std::endl;
        }

        std::cout << "Files to download: " << seed.files.size() << std::endl;

        for (const auto& file : seed.files) {
            std::cout << "\n=== Processing file: " << file.name << " ===" << std::endl;
            std::cout << "  Size: " << file.size << " bytes" << std::endl;
            std::cout << "  Blocks: " << file.blocks.size() << std::endl;

            std::vector<BlockData> downloaded_blocks;

            for (const auto& block : file.blocks) {
                std::cout << "  Downloading block " << block.id
                          << " (bytes " << block.range_start << "-" << block.range_end << ")... ";

                auto data = download_block(block);
                std::cout << data.size() << " bytes downloaded" << std::endl;

                downloaded_blocks.push_back({block.id, std::move(data)});
            }

            assemble_file(file, downloaded_blocks, output_dir);
        }

        std::cout << "\nAll files downloaded and verified successfully." << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
