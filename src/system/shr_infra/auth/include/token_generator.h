#pragma once

#include <string>

namespace auth
{

    /**
     * @file token_generator.h
     * @brief Stateless token generation utility
     *
     * Generates cryptographically random 128-bit tokens formatted as UUID v4 strings.
     * No dependencies, no state, fully thread-safe by design.
     *
     * Reads directly from /dev/urandom - no third-party crypto libraries,
     * no rand(), no system state manipulation.
     */
    namespace token_generator
    {

        /**
         * Generates a random 128-bit token as a UUID v4 string.
         *
         * Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
         * - First 48 bits: random
         * - Next 4 bits: version (always 4 for random)
         * - Next 12 bits: random
         * - Next 2 bits: variant (10 for RFC 4122)
         * - Last 62 bits: random
         *
         * Reads 16 bytes from /dev/urandom. If /dev/urandom is unavailable
         * (extremely rare on Unix-like systems), returns an empty string.
         *
         * @return 36-character UUID v4 string, or empty string on failure
         * @thread_safe Yes - no shared state, pure function
         * @async_signal_safe No - calls open/read/close (not async-safe)
         *
         * Example output:
         *   "550e8400-e29b-41d4-a716-446655440000"
         */
        std::string generate();

    } // namespace token_generator

} // namespace auth