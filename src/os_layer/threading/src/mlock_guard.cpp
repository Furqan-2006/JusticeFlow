#include "../include/mlock_guard.h"
#include <sys/mman.h>
#include <string.h>
#include <iostream>

mlock_guard::mlock_guard(void* address, size_t size) : addr(address), len(size) {
    if (mlock(addr, len) != 0) {
        std::cerr << "[Memory] Warning: mlock failed. Check RLIMIT_MEMLOCK limits.\n";
    }
}

mlock_guard::~mlock_guard() {
    // Architecture Mandate: Securely zero out the credential buffer before unlocking
    memset(addr, 0, len);
    munlock(addr, len);
}
