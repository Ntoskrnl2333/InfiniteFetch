#ifndef SEED_PARSER_H
#define SEED_PARSER_H

#include "infetch.h"

SeedInfo parse_seed(const std::string& filepath);

// Verify that the SHA256 of the seed file matches the declared hash.
// Returns true if hash is empty (no verification) or matches.
bool verify_seed_hash(const std::string& filepath, const std::string& expected_hash);

#endif // SEED_PARSER_H
