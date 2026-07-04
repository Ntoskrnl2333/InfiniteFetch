#include "downloader.h"

#include <httplib.h>

#include <windows.h>
#include <wininet.h>
#include <windns.h>
#include <sql.h>
#include <sqlext.h>
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

// Find option by class and name, returning int value
static int find_int_option(const std::vector<Option>& options,
                            const std::string& option_class,
                            const std::string& name,
                            int default_value) {
    std::string val = find_option(options, option_class, name);
    if (val.empty()) return default_value;
    try {
        return std::stoi(val);
    } catch (...) {
        return default_value;
    }
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

    // Configure timeout from options (default 30 seconds)
    int timeout_secs = find_int_option(link.options, "timeout", "seconds", 30);
    cli.set_connection_timeout(timeout_secs, 0);
    cli.set_read_timeout(timeout_secs, 0);
    cli.set_write_timeout(timeout_secs, 0);

    // Get retry count from options (default 0 = no retry)
    int retry_count = find_int_option(link.options, "retry", "count", 0);

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

    // Retry loop
    for (int attempt = 0; attempt <= retry_count; attempt++) {
        if (attempt > 0) {
            std::cerr << "Retry attempt " << attempt << "/" << retry_count << " for "
                      << url << std::endl;
        }

        auto res = cli.Get(path.c_str(), headers);

        if (!res) {
            std::cerr << "HTTP request failed: " << httplib::to_string(res.error()) << std::endl;
            if (attempt < retry_count) continue;
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
            if (status >= 500 && attempt < retry_count) continue; // retry on server errors
            return false;
        }

        const auto& body = res->body;
        out_data.assign(body.begin(), body.end());
        return true;
    }

    return false;
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

// Download via DNS TXT records using Windows DNS API
static bool download_dns(const LinkInfo& link, const BlockInfo& block,
                          std::vector<uint8_t>& out_data) {
    std::string domain = build_url(link);

    PDNS_RECORD pDnsRecord = nullptr;
    DNS_STATUS status = DnsQuery_A(
        domain.c_str(),
        DNS_TYPE_TEXT,
        DNS_QUERY_STANDARD,
        nullptr,
        &pDnsRecord,
        nullptr
    );

    if (status != 0 || !pDnsRecord) {
        std::cerr << "DNS query failed for " << domain
                  << ": " << status << std::endl;
        return false;
    }

    // Concatenate all TXT record strings
    std::string txt_data;
    for (PDNS_RECORD pRec = pDnsRecord; pRec != nullptr; pRec = pRec->pNext) {
        if (pRec->wType == DNS_TYPE_TEXT) {
            // DNS TXT records store data as length-prefixed strings
            for (DWORD i = 0; i < pRec->Data.TXT.dwStringCount; i++) {
                txt_data += pRec->Data.TXT.pStringArray[i];
            }
        }
    }

    DnsRecordListFree(pDnsRecord, DnsFreeRecordList);

    if (txt_data.empty()) {
        std::cerr << "No TXT records found for " << domain << std::endl;
        return false;
    }

    // Extract the requested range from the concatenated data
    int64_t total_size = static_cast<int64_t>(txt_data.size());
    int64_t start = std::max<int64_t>(0, block.range_start);
    int64_t end = std::min<int64_t>(total_size - 1, block.range_end);

    if (start >= total_size) {
        std::cerr << "Block range " << block.range_start << "-" << block.range_end
                  << " exceeds DNS data size " << total_size << std::endl;
        return false;
    }

    out_data.assign(txt_data.begin() + static_cast<ptrdiff_t>(start),
                    txt_data.begin() + static_cast<ptrdiff_t>(end) + 1);

    return true;
}

// Download via SQL/ODBC query
static bool download_sql(const LinkInfo& link, const BlockInfo& block,
                          std::vector<uint8_t>& out_data) {
    std::string query = build_url(link);

    // Find connection string from options
    std::string conn_str = find_option(link.options, "sql", "connection");
    if (conn_str.empty()) {
        std::cerr << "No SQL connection string found in options" << std::endl;
        return false;
    }

    SQLHENV hEnv = nullptr;
    SQLHDBC hDbc = nullptr;
    SQLHSTMT hStmt = nullptr;
    bool success = false;

    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv) != SQL_SUCCESS) {
        std::cerr << "SQLAllocHandle(ENV) failed" << std::endl;
        return false;
    }

    SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

    if (SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc) != SQL_SUCCESS) {
        std::cerr << "SQLAllocHandle(DBC) failed" << std::endl;
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return false;
    }

    // Connect to database
    SQLCHAR outConnStr[1024];
    SQLSMALLINT outConnStrLen = 0;
    SQLRETURN ret = SQLDriverConnectA(hDbc, nullptr,
        (SQLCHAR*)conn_str.c_str(), (SQLSMALLINT)conn_str.size(),
        outConnStr, sizeof(outConnStr), &outConnStrLen,
        SQL_DRIVER_NOPROMPT);

    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLCHAR sqlState[6], errMsg[256];
        SQLINTEGER nativeErr;
        SQLSMALLINT msgLen;
        SQLGetDiagRecA(SQL_HANDLE_DBC, hDbc, 1, sqlState, &nativeErr, errMsg, sizeof(errMsg), &msgLen);
        std::cerr << "SQL connection failed: " << errMsg << std::endl;
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return false;
    }

    // Execute query
    if (SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt) != SQL_SUCCESS) {
        std::cerr << "SQLAllocHandle(STMT) failed" << std::endl;
        SQLDisconnect(hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return false;
    }

    ret = SQLExecDirectA(hStmt, (SQLCHAR*)query.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLCHAR sqlState[6], errMsg[256];
        SQLINTEGER nativeErr;
        SQLSMALLINT msgLen;
        SQLGetDiagRecA(SQL_HANDLE_STMT, hStmt, 1, sqlState, &nativeErr, errMsg, sizeof(errMsg), &msgLen);
        std::cerr << "SQL query failed: " << errMsg << std::endl;
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        SQLDisconnect(hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return false;
    }

    // Fetch first row, first column as binary data
    ret = SQLFetch(hStmt);
    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        SQLLEN indicator = 0;
        std::vector<uint8_t> buffer(65536);

        ret = SQLGetData(hStmt, 1, SQL_C_BINARY, buffer.data(), (SQLLEN)buffer.size(), &indicator);

        if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
            if (indicator == SQL_NULL_DATA) {
                std::cerr << "SQL query returned NULL data" << std::endl;
            } else if (indicator > 0) {
                size_t data_len = static_cast<size_t>(indicator);
                if (data_len > buffer.size()) {
                    // Data too large for buffer, re-read with larger buffer
                    buffer.resize(data_len);
                    ret = SQLGetData(hStmt, 1, SQL_C_BINARY, buffer.data(), (SQLLEN)buffer.size(), &indicator);
                }
                buffer.resize(data_len);
                out_data = std::move(buffer);
                success = true;
            }
        } else if (ret == SQL_NO_DATA) {
            std::cerr << "SQL query returned no data" << std::endl;
        }
    } else {
        std::cerr << "SQL fetch returned no rows" << std::endl;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    SQLDisconnect(hDbc);
    SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
    SQLFreeHandle(SQL_HANDLE_ENV, hEnv);

    return success;
}

std::vector<uint8_t> download_block(const BlockInfo& block) {
    for (const auto& link : block.links) {
        std::vector<uint8_t> data;

        bool ok = false;
        if (link.linktype == "http" || link.linktype == "https") {
            ok = download_http(link, block, data);
        } else if (link.linktype == "ftp") {
            ok = download_ftp(link, block, data);
        } else if (link.linktype == "dns") {
            ok = download_dns(link, block, data);
        } else if (link.linktype == "sql") {
            ok = download_sql(link, block, data);
        } else {
            std::cerr << "Unsupported link type: " << link.linktype << std::endl;
            continue;
        }

        if (ok) {
            int64_t expected_size = block.range_end - block.range_start + 1;
            if (expected_size > 0 && static_cast<int64_t>(data.size()) != expected_size) {
                std::cout << "Block " << block.id << " size differs from range: expected "
                          << expected_size << " bytes, got " << data.size()
                          << " bytes (hash verification will catch corruption)" << std::endl;
            }
            return data;
        }

        std::cerr << "Failed to download block " << block.id
                  << " from " << link.linktype << "://" << link.link
                  << ", trying next link..." << std::endl;
    }

    throw std::runtime_error("Failed to download block " + std::to_string(block.id) +
                             " from all available links");
}
