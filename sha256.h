#ifndef SHA256_H
#define SHA256_H

#include <string>
#include <vector>
#include <cstdint>

// Compute SHA256 hash of data, returns lowercase hex string
std::string sha256_hex(const std::vector<uint8_t>& data);
std::string sha256_hex(const uint8_t* data, size_t len);

#endif // SHA256_H
