clovervm_add_native_module(_time
    SOURCES
        native_modules/time_module.c)

clovervm_add_native_module(_os
    SOURCES
        native_modules/os_module.c)

clovervm_add_native_module(_math
    SOURCES
        native_modules/math_module.c)

clovervm_add_native_module(_errno
    SOURCES
        native_modules/errno_module.c)
