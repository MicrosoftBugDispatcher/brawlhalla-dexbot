#pragma once
#include <windows.h>
#include <vector>
#include <string>

namespace Memory {
    // Internal generic pattern scanner
    uintptr_t FindPattern(const char* moduleName, const char* pattern);
}
