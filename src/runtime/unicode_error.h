#ifndef CL_UNICODE_ERROR_H
#define CL_UNICODE_ERROR_H

namespace cl
{
    class VirtualMachine;
    class ModuleObject;
    void install_unicode_error_helpers(VirtualMachine *, ModuleObject *);
    void install_unicode_error_methods(VirtualMachine *, ModuleObject *);
}  // namespace cl

#endif  // CL_UNICODE_ERROR_H
