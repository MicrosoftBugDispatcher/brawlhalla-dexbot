#include "Memory.h"
#include <psapi.h>

#define INRANGE(x,a,b)  (x >= a && x <= b) 
#define getBits( x )    (INRANGE((x&(~0x20)),'A','F') ? ((x&(~0x20)) - 'A' + 0xa) : (INRANGE(x,'0','9') ? x - '0' : 0))
#define getByte( x )    (getBits(x[0]) << 4 | getBits(x[1]))

uintptr_t Memory::FindPattern(const char* moduleName, const char* pattern) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    
    uintptr_t currentAddr = (uintptr_t)sysInfo.lpMinimumApplicationAddress;
    uintptr_t maxAddr = (uintptr_t)sysInfo.lpMaximumApplicationAddress;
    
    MEMORY_BASIC_INFORMATION mbi;
    while (currentAddr < maxAddr) {
        if (!VirtualQuery((LPCVOID)currentAddr, &mbi, sizeof(mbi))) break;
        
        bool isReadable = (mbi.State == MEM_COMMIT) && 
                          ((mbi.Protect & PAGE_READONLY) || (mbi.Protect & PAGE_READWRITE) || 
                           (mbi.Protect & PAGE_EXECUTE_READ) || (mbi.Protect & PAGE_EXECUTE_READWRITE)) &&
                          !(mbi.Protect & PAGE_GUARD);
                           
        if (isReadable) {
            uintptr_t rangeStart = currentAddr;
            uintptr_t rangeEnd = currentAddr + mbi.RegionSize;
            
            const char* pat = pattern;
            uintptr_t firstMatch = 0;
            
            __try {
                for (uintptr_t pCur = rangeStart; pCur < rangeEnd; pCur++) {
                    if (!*pat) {
                        uint8_t b1 = *(uint8_t*)firstMatch;
                        if (b1 != 0xFF && b1 != 0x48) return firstMatch;
                        pat = pattern; pCur = firstMatch; firstMatch = 0; continue;
                    }

                    if (*(PBYTE)pat == '\?' || *(BYTE*)pCur == getByte(pat)) {
                        if (!firstMatch) firstMatch = pCur;
                        
                        if (!pat[2]) {
                            uint8_t b1 = *(uint8_t*)firstMatch;
                            if (b1 != 0xFF && b1 != 0x48) return firstMatch;
                            pat = pattern; pCur = firstMatch; firstMatch = 0; continue;
                        }
                        
                        if (*(PWORD)pat == '\?\?' || *(PBYTE)pat != '\?') pat += 3;
                        else pat += 2;
                    } else {
                        pat = pattern;
                        if (firstMatch) {
                            pCur = firstMatch;
                            firstMatch = 0;
                        }
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                // Ignore pages that threw exceptions despite protection checks
            }
        }
        currentAddr += mbi.RegionSize;
    }
    return 0;
}
