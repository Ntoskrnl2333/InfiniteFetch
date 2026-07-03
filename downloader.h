#ifndef DOWNLOADER_H
#define DOWNLOADER_H

#include "infetch.h"
#include <vector>
#include <cstdint>

// Download a single block using its available links.
// Tries links in order until one succeeds.
// Returns the downloaded data on success.
std::vector<uint8_t> download_block(const BlockInfo& block);

#endif // DOWNLOADER_H
