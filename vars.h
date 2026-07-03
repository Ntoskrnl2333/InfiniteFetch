#define VERSION "0.0.1"

#ifdef _WIN32
#define PLATFORM "Windows"
#elif __linux__
#define PLATFORM "Linux"
#elif __APPLE__
#define PLATFORM "macOS"
#else
#define PLATFORM "Unknown"
#endif
