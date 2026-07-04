#include "seed_parser.h"
#include "sha256.h"

#include <pugixml.hpp>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <cstdint>

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Safely parse string to int64_t, returning 0 on failure
static int64_t safe_stoll(const std::string& s) {
    if (s.empty()) return 0;
    try {
        return std::stoll(s);
    } catch (...) {
        return 0;
    }
}

// Safely parse string to int, returning 0 on failure
static int safe_stoi(const std::string& s) {
    if (s.empty()) return 0;
    try {
        return std::stoi(s);
    } catch (...) {
        return 0;
    }
}

SeedInfo parse_seed(const std::string& filepath) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filepath.c_str());

    if (!result) {
        throw std::runtime_error(std::string("Failed to parse seed file: ") + result.description());
    }

    auto root = doc.child("infetch");
    if (!root) {
        throw std::runtime_error("Missing <infetch> root element");
    }

    SeedInfo seed;
    seed.hash = root.attribute("hash").as_string();
    seed.title = root.child_value("title");
    seed.charset = root.child_value("charset");

    auto filesNode = root.child("files");
    if (!filesNode) {
        throw std::runtime_error("Missing <files> element");
    }

    for (auto fileNode = filesNode.child("file"); fileNode; fileNode = fileNode.next_sibling("file")) {
        FileInfo file;
        file.name = fileNode.attribute("name").as_string();
        file.hash = fileNode.child_value("hash");
        file.size = safe_stoll(fileNode.child_value("size"));
        file.create_time = fileNode.child_value("create_time");
        file.edit_time = fileNode.child_value("edit_time");

        auto blocksNode = fileNode.child("blocks");
        if (!blocksNode) {
            throw std::runtime_error("Missing <blocks> in file: " + file.name);
        }

        for (auto blockNode = blocksNode.child("block"); blockNode; blockNode = blockNode.next_sibling("block")) {
            BlockInfo block;
            block.id = safe_stoi(blockNode.attribute("id").as_string());
            block.hash = blockNode.attribute("hash").as_string();

            // Parse range "start-end"
            std::string rangeStr = blockNode.attribute("range").as_string();
            auto parts = split(rangeStr, '-');
            if (parts.size() == 2) {
                block.range_start = safe_stoll(parts[0]);
                block.range_end = safe_stoll(parts[1]);
            }

            block.uploader = blockNode.child_value("uploader");

            // Build the LinkInfo for this block
            LinkInfo link;
            link.linktype = blockNode.child_value("linktype");
            link.link = blockNode.child_value("link");
            link.uploader = block.uploader;

            // Parse options
            auto optionsNode = blockNode.child("options");
            if (optionsNode) {
                for (auto optNode = optionsNode.child("option"); optNode; optNode = optNode.next_sibling("option")) {
                    Option opt;
                    opt.option_class = optNode.attribute("class").as_string();
                    opt.name = optNode.attribute("name").as_string();
                    opt.value = optNode.child_value();
                    link.options.push_back(opt);
                }
            }

            block.links.push_back(link);

            file.blocks.push_back(block);
        }

        seed.files.push_back(file);
    }

    return seed;
}

bool verify_seed_hash(const std::string& filepath, const std::string& expected_hash) {
    if (expected_hash.empty()) {
        return true; // No hash declared, skip verification
    }

    // Read the entire seed file as text
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Warning: Cannot open seed file for hash verification" << std::endl;
        return false;
    }

    size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::string raw_text(file_size, '\0');
    file.read(raw_text.data(), file_size);
    file.close();

    // Find the inner content between <infetch ...> and </infetch>
    // Locate the end of the opening tag: first '>' after "<infetch"
    size_t tag_start = raw_text.find("<infetch");
    if (tag_start == std::string::npos) {
        std::cerr << "Warning: Cannot find <infetch> tag in seed file" << std::endl;
        return false;
    }

    size_t open_end = raw_text.find('>', tag_start);
    if (open_end == std::string::npos) {
        std::cerr << "Warning: Malformed <infetch> opening tag" << std::endl;
        return false;
    }
    open_end++; // Move past '>'

    // Locate the closing tag </infetch>
    size_t close_start = raw_text.rfind("</infetch>");
    if (close_start == std::string::npos || close_start < open_end) {
        std::cerr << "Warning: Cannot find </infetch> closing tag" << std::endl;
        return false;
    }

    // Extract and hash the inner content (between tags, exclusive)
    std::string inner = raw_text.substr(open_end, close_start - open_end);
    std::vector<uint8_t> inner_data(inner.begin(), inner.end());
    std::string actual_hash = sha256_hex(inner_data);

    // Compare case-insensitively
    std::string expected_lower = expected_hash;
    std::string actual_lower = actual_hash;
    for (auto& c : expected_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : actual_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (expected_lower != actual_lower) {
        std::cerr << "Seed hash mismatch!\n"
                  << "  Expected: " << expected_hash << "\n"
                  << "  Got:      " << actual_hash << std::endl;
        return false;
    }

    return true;
}
