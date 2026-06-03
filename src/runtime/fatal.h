#ifndef CL_FATAL_H
#define CL_FATAL_H

#include <string>

namespace cl
{
    [[noreturn]] void fatal(const char *message);
    [[noreturn]] void fatal(const std::string &message);
}  // namespace cl

#endif  // CL_FATAL_H
