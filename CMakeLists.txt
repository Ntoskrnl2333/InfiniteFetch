cmake_minimum_required(VERSION 3.20)
project(InfiniteFetch VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# pugixml — lightweight XML parser
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.15
)
FetchContent_MakeAvailable(pugixml)

# cpp-httplib — header-only HTTP/HTTPS client
FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.20.0
)
FetchContent_MakeAvailable(cpp-httplib)

add_executable(infetch
    main.cpp
    seed_parser.cpp
    downloader.cpp
    assembler.cpp
    sha256.cpp
)

target_include_directories(infetch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${pugixml_SOURCE_DIR}/src
    ${cpp-httplib_SOURCE_DIR}
)

target_link_libraries(infetch PRIVATE
    pugixml
    ws2_32      # Winsock for cpp-httplib
    crypt32     # CertOpenSystemStore etc. for cpp-httplib SSL
    wininet     # FTP support
    bcrypt      # SHA256 via CNG
    dnsapi      # DNS TXT record queries
)

# Platform defines
if(WIN32)
    target_compile_definitions(infetch PRIVATE _WIN32_WINNT=0x0A00)  # Windows 10+
endif()
