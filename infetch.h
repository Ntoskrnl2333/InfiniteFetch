#ifndef INFETCH_H
#define INFETCH_H

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

struct Option {
    std::string option_class; // "header" or "status_code"
    std::string name;
    std::string value;
};

struct LinkInfo {
    std::string linktype; // "http", "https", "ftp"
    std::string link;
    std::string uploader;
    std::vector<Option> options;
};

struct BlockInfo {
    int id = 0;
    int64_t range_start = 0;
    int64_t range_end = 0;
    std::string hash;   // expected SHA256 of this block
    std::string uploader;
    std::vector<LinkInfo> links;
};

struct FileInfo {
    std::string name;
    std::string hash;   // expected SHA256 of the whole file
    int64_t size = 0;
    std::string create_time;
    std::string edit_time;
    std::vector<BlockInfo> blocks;
};

struct SeedInfo {
    std::string hash;   // SHA256 of the seed file itself
    std::string title;
    std::string charset;
    std::vector<FileInfo> files;
};

#endif // INFETCH_H
