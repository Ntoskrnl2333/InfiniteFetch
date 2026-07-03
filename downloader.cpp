#include "downloader.h"

#include <httplib.h>

#include <windows.h>
#include <wininet.h>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <cstring>

#pragma comment(lib, "wininet.lib")

// Build a URL from link and optional path components
static std::string build_url(const LinkInfo& link) {
    return link.link;
}

// Find option by class and name
static std::string find_option(const std::vector<Option>& options,
                                const std::string& option_class,
                                const std::string& name) {
    for (const auto& opt : options) {
        if (opt.option_class == option_class && opt.name == name) {
            return opt.value;
        }
    }
    return {};
}

// Handle status-code-based option values.
// Returns: empty string = proceed normally, "ERR" = fatal error,
//           anything else = treat as zero-fill pattern
static std::string find_status_code_option(const std::vector<Option>& options, int status_code) {
    return find_option(options, "status_code", std::to_string(status_code));
}

// Parse status_code option value and handle it.
// Returns true if the caller should continue; false if the link should be abandoned.
// If out_data_filled is true, out_data has been filled with the appropriate content.
static bool handle_status_code_option(const std::string& option_value,
                                       const BlockInfo& block,
                                       std::vector<uint8_t>& out_data,
                                       bool& out_data_filled) {
    out_data_filled = false;

    if (option_value.empty()) return true;

    std::string upper = option_value;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    if (upper.find("ERR") != std::string::npos) {
        return false;
    }

    // Handle "return 0000h" or "return XXXXh" patterns
    // Fill with zeros at the expected block size
    if (upper.find("RETURN") != std::string::npos) {
        int64_t block_size = block.range_end - block.range_start + 1;
        if (block_size > 0) {
            out_data.assign(static_cast<size_t>(block_size), 0);
            out_data_filled = true;
            return true;
        }
    }

    return true;
}

// Download via HTTP/HTTPS using cpp-httplib
static bool download_http(const LinkInfo& link, const BlockInfo& block,
                          std::vector<uint8_t>& out_data) {
    std::string url = build_url(link);

    // Parse host and path from URL
    // Simple URL parsing: scheme://host/path
    std::string scheme = "http";
    std::string host;
    std::string path = "/";

    size_t scheme_end = url.find("://");
    if (scheme_end != std::string::npos) {
        scheme = url.substr(0, scheme_end);
        size_t host_start = scheme_end + 3;
        size_t path_start = url.find('/', host_start);
        if (path_start != std::string::npos) {
            host = url.substr(host_start, path_start - host_start);
            path = url.substr(path_start);
        } else {
            host = url.substr(host_start);
            path = "/";
        }
    } else {
        host = url;
    }

    httplib::Client cli(scheme + "://" + host);

    httplib::Headers headers;

    // Build Range header from block range
    std::string range_header = "bytes=" + std::to_string(block.range_start) + "-" +
                               std::to_string(block.range_end);
    headers.emplace("Range", range_header);

    // Add custom headers from options
    for (const auto& opt : link.options) {
        if (opt.option_class == "header" && opt.name != "Range") {
            headers.emplace(opt.name, opt.value);
        }
    }

    auto res = cli.Get(path.c_str(), headers);

    if (!res) {
        std::cerr << "HTTP request failed: " << httplib::to_string(res.error()) << std::endl;
        return false;
    }

    int status = res->status;

    // Check for status_code options that override normal behavior
    std::string status_opt = find_status_code_option(link.options, status);
    bool data_filled = false;
    if (!handle_status_code_option(status_opt, block, out_data, data_filled)) {
        std::cerr << "Status code " << status << " treated as error by seed options" << std::endl;
        return false;
    }
    if (data_filled) {
        std::cout << "Status code " << status << " handled: returning zero-filled block" << std::endl;
        return true;
    }

    if (status != 200 && status != 206) {
        std::cerr << "HTTP status: " << status << std::endl;
        return false;
    }

    const auto& body = res->body;
    out_data.assign(body.begin(), body.end());
    return true;
}

// Download via FTP using WinINet
static bool download_ftp(const LinkInfo& link, const BlockInfo& block,
                          std::vector<uint8_t>& out_data) {
    std::string url = build_url(link);

    HINTERNET hInternet = InternetOpenA("InfiniteFetch", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hInternet) {
        std::cerr << "InternetOpen failed: " << GetLastError() << std::endl;
        return false;
    }

    HINTERNET hFtp = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0,
                                       INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hFtp) {
        std::cerr << "InternetOpenUrl failed for FTP: " << GetLastError() << std::endl;
        InternetCloseHandle(hInternet);
        return false;
    }

    const DWORD buf_size = 65536;
    std::vector<uint8_t> buffer(buf_size);
    DWORD bytes_read = 0;

    // For FTP with range, we read from the start offset and take the needed bytes
    int64_t total_needed = block.range_end - block.range_start + 1;

    // Seek to range_start if possible via FTP REST command
    // WinINet doesn't directly expose REST, so we read and skip
    int64_t skipped = 0;
    while (skipped < block.range_start) {
        DWORD to_read = static_cast<DWORD>(std::min<int64_t>(buf_size, block.range_start - skipped));
        if (!InternetReadFile(hFtp, buffer.data(), to_read, &bytes_read) || bytes_read == 0) {
            break;
        }
        skipped += bytes_read;
    }

    out_data.clear();
    out_data.reserve(static_cast<size_t>(total_needed));

    while (static_cast<int64_t>(out_data.size()) < total_needed) {
        DWORD to_read = static_cast<DWORD>(
            std::min<int64_t>(buf_size, total_needed - out_data.size()));
        if (!InternetReadFile(hFtp, buffer.data(), to_read, &bytes_read) || bytes_read == 0) {
            break;
        }
        out_data.insert(out_data.end(), buffer.data(), buffer.data() + bytes_read);
    }

    InternetCloseHandle(hFtp);
    InternetCloseHandle(hInternet);

    return out_data.size() >= static_cast<size_t>(total_needed);
}

std::vector<uint8_t> download_block(const BlockInfo& block) {
    for (const auto& link : block.links) {
        std::vector<uint8_t> data;

        bool ok = false;
        if (link.linktype == "http" || link.linktype == "https") {
            ok = download_http(link, block, data);
        } else if (link.linktype == "ftp") {
            ok = download_ftp(link, block, data);
        } else {
            std::cerr << "Unsupported link type: " << link.linktype << std::endl;
            continue;
        }

        if (ok) {
            return data;
        }

        std::cerr << "Failed to download block " << block.id
                  << " from " << link.linktype << "://" << link.link
                  << ", trying next link..." << std::endl;
    }

    throw std::runtime_error("Failed to download block " + std::to_string(block.id) +
                             " from all available links");
}
