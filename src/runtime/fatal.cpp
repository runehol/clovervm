#include "runtime/fatal.h"

#include <cstdio>
#include <cstdlib>

namespace cl
{
    [[noreturn]] void fatal(const char *message)
    {
        std::fprintf(stderr, "clovervm fatal: %s\n", message);
        std::abort();
    }

    [[noreturn]] void fatal(const std::string &message)
    {
        fatal(message.c_str());
    }
}  // namespace cl
