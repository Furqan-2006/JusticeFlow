#include "../include/token_generator.h"
#include "common/logger.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cerrno>

namespace auth
{
    namespace token_generator
    {

        std::string generate()
        {
            // Read 16 random bytes from /dev/urandom
            unsigned char random_bytes[16];

            int fd = open("/dev/urandom", O_RDONLY);
            if (fd == -1)
            {
                Logger::error("[TokenGenerator] Failed to open /dev/urandom");
                return "";
            }

            ssize_t bytes_read = read(fd, random_bytes, sizeof(random_bytes));
            close(fd);

            if (bytes_read != sizeof(random_bytes))
            {
                Logger::error("[TokenGenerator] Failed to read 16 bytes from /dev/urandom");
                return "";
            }

            // Set UUID v4 format:
            // - time_hi_and_version: set version to 4 (bits 12-15)
            // - clock_seq_hi_and_reserved: set variant to RFC 4122 (bits 6-7 to 10)

            random_bytes[6] = (random_bytes[6] & 0x0f) | 0x40; // Version 4
            random_bytes[8] = (random_bytes[8] & 0x3f) | 0x80; // Variant 10

            // Format as UUID string: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
            char uuid_str[37];
            std::snprintf(uuid_str, sizeof(uuid_str),
                          "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                          random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3],
                          random_bytes[4], random_bytes[5],
                          random_bytes[6], random_bytes[7],
                          random_bytes[8], random_bytes[9],
                          random_bytes[10], random_bytes[11], random_bytes[12], random_bytes[13],
                          random_bytes[14], random_bytes[15]);

            return std::string(uuid_str);
        }

    } // namespace token_generator
} // namespace auth