#ifndef CLOVERVM_CLOVERVM_H
#define CLOVERVM_CLOVERVM_H

#ifdef _WIN32
#ifdef CLOVERVM_BUILDING_SHARED
#define CL_EXPORT __declspec(dllexport)
#else
#define CL_EXPORT __declspec(dllimport)
#endif
#else
#define CL_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum clover_status
    {
        CLOVER_STATUS_OK = 0,
        CLOVER_STATUS_ERROR = -1,
    } clover_status;

    typedef struct clover_vm clover_vm;

    CL_EXPORT clover_vm *clover_vm_new(void);
    CL_EXPORT void clover_vm_destroy(clover_vm *vm);
    CL_EXPORT void clover_vm_set_trace_instructions(clover_vm *vm, int enabled);
    CL_EXPORT clover_status clover_vm_run_file(clover_vm *vm, const char *path,
                                               int print_bytecode);
    CL_EXPORT clover_status clover_vm_run_string(clover_vm *vm,
                                                 const char *source,
                                                 int print_bytecode);
    CL_EXPORT clover_status clover_vm_run_interactive(clover_vm *vm,
                                                      int print_bytecode);

#ifdef __cplusplus
}
#endif

#endif  // CLOVERVM_CLOVERVM_H
