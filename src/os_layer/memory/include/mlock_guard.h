#pragma once

#include <stddef.h>

class mlock_guard
{
private:
    void *addr;
    size_t len;

public:
    mlock_guard(void *address, size_t size);
    ~mlock_guard();

    // Prevent copying to avoid double-unocking or zeroing early
    mlock_guard(const mlock_guard &) = delete;
    mlock_guard &operator=(const mlock_guard &) = delete;
};

#endif // MLOCK_GUARD_H
