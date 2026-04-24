/**
 * @file mlock_guard.h
 * @brief RAII wrapper for memory locking via mlock(2).
 *
 * Ensures sensitive credential buffers remain in physical RAM and cannot be
 * swapped to disk. On destruction, overwrites memory with zeros before unlocking.
 *
 * Usage:
 *   void* cred_buffer = malloc(256);
 *   mlock_guard guard(cred_buffer, 256);  // Locks into RAM, zeros on destruct
 */

#pragma once

#include <stddef.h>

class mlock_guard
{
private:
    void *addr;
    size_t len;

public:
    /**
     * Locks memory region into physical RAM to prevent swap.
     * Respects RLIMIT_MEMLOCK; if limit exceeded, mlock(2) fails gracefully.
     */
    mlock_guard(void *address, size_t size);

    /**
     * Overwrites memory with zeros (secure wipe) then unlocks.
     * This guarantees sensitive data cannot be recovered from swap or core dumps.
     */
    ~mlock_guard();

    // Prevent copying to avoid double-unlocking or premature zeroing
    mlock_guard(const mlock_guard &) = delete;
    mlock_guard &operator=(const mlock_guard &) = delete;

    // Prevent moving (would invalidate RAII contract)
    mlock_guard(mlock_guard &&) = delete;
    mlock_guard &operator=(mlock_guard &&) = delete;
};

#endif // MLOCK_GUARD_H
