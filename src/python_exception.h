#ifndef CL_PYTHON_EXCEPTION_H
#define CL_PYTHON_EXCEPTION_H

#include <exception>
#include <string>

namespace cl
{
    class PythonException : public std::exception
    {
    public:
        explicit PythonException(std::wstring message);

        const char *what() const noexcept override
        {
            return narrow_message.c_str();
        }

        const std::wstring &wide_what() const noexcept { return wide_message; }

    private:
        std::wstring wide_message;
        std::string narrow_message;
    };
}  // namespace cl

#endif  // CL_PYTHON_EXCEPTION_H
