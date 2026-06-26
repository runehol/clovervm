#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <clovervm/native_module.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static clover_handle raise_errno(clover_context *ctx)
{
    return clover_raise_value_error(ctx, strerror(errno));
}

typedef struct
{
    char *data;
} path_string;

static clover_status path_string_init(clover_context *ctx, clover_handle value,
                                      path_string *out)
{
    size_t size;
    out->data = NULL;
    if(clover_string_as_utf8(ctx, value, NULL, 0, &size) != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }

    char *buffer = (char *)malloc(size + 1);
    if(buffer == NULL)
    {
        (void)clover_raise_value_error(ctx, "out of memory");
        return CLOVER_STATUS_ERROR;
    }

    if(clover_string_as_utf8(ctx, value, buffer, size + 1, &size) !=
       CLOVER_STATUS_OK)
    {
        free(buffer);
        return CLOVER_STATUS_ERROR;
    }

    out->data = buffer;
    return CLOVER_STATUS_OK;
}

static void path_string_destroy(path_string *path)
{
    free(path->data);
    path->data = NULL;
}

static clover_status value_as_int(clover_context *ctx, clover_handle value,
                                  int *out)
{
    int64_t raw;
    if(clover_int_as_int64(ctx, value, &raw) != CLOVER_STATUS_OK)
    {
        return CLOVER_STATUS_ERROR;
    }
    if(raw < INT_MIN || raw > INT_MAX)
    {
        (void)clover_raise_value_error(ctx, "integer argument out of range");
        return CLOVER_STATUS_ERROR;
    }
    *out = (int)raw;
    return CLOVER_STATUS_OK;
}

static clover_handle os_getcwd(clover_context *ctx)
{
    char *cwd = getcwd(NULL, 0);
    if(cwd == NULL)
    {
        return raise_errno(ctx);
    }
    clover_handle result = clover_string_from_utf8(ctx, cwd);
    free(cwd);
    return result;
}

static clover_handle os_chdir(clover_context *ctx, clover_handle path_value)
{
    path_string path;
    if(path_string_init(ctx, path_value, &path) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    int rc = chdir(path.data);
    int saved_errno = errno;
    path_string_destroy(&path);
    if(rc != 0)
    {
        errno = saved_errno;
        return raise_errno(ctx);
    }
    return clover_none(ctx);
}

static clover_handle os_listdir(clover_context *ctx, clover_handle path_value)
{
    path_string path;
    if(path_string_init(ctx, path_value, &path) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }

    DIR *dir = opendir(path.data);
    int opendir_errno = errno;
    path_string_destroy(&path);
    if(dir == NULL)
    {
        errno = opendir_errno;
        return raise_errno(ctx);
    }

    size_t count = 0;
    size_t capacity = 16;
    clover_handle *items = (clover_handle *)malloc(capacity * sizeof(*items));
    if(items == NULL)
    {
        closedir(dir);
        return clover_raise_value_error(ctx, "out of memory");
    }

    errno = 0;
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }
        if(count == capacity)
        {
            capacity *= 2;
            clover_handle *grown =
                (clover_handle *)realloc(items, capacity * sizeof(*items));
            if(grown == NULL)
            {
                free(items);
                closedir(dir);
                return clover_raise_value_error(ctx, "out of memory");
            }
            items = grown;
        }
        items[count] = clover_string_from_utf8(ctx, entry->d_name);
        ++count;
    }
    int readdir_errno = errno;
    closedir(dir);
    if(readdir_errno != 0)
    {
        free(items);
        errno = readdir_errno;
        return raise_errno(ctx);
    }

    clover_handle result = clover_tuple_from_array(ctx, items, count);
    free(items);
    return result;
}

static clover_handle os_getpid(clover_context *ctx)
{
    return clover_int_from_int64(ctx, (int64_t)getpid());
}

static clover_handle os_getppid(clover_context *ctx)
{
    return clover_int_from_int64(ctx, (int64_t)getppid());
}

static clover_handle os_getuid(clover_context *ctx)
{
    return clover_int_from_int64(ctx, (int64_t)getuid());
}

static clover_handle os_geteuid(clover_context *ctx)
{
    return clover_int_from_int64(ctx, (int64_t)geteuid());
}

static clover_handle os_getgid(clover_context *ctx)
{
    return clover_int_from_int64(ctx, (int64_t)getgid());
}

static clover_handle os_getegid(clover_context *ctx)
{
    return clover_int_from_int64(ctx, (int64_t)getegid());
}

static clover_handle os_getenv(clover_context *ctx, clover_handle name_value)
{
    path_string name;
    if(path_string_init(ctx, name_value, &name) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    const char *value = getenv(name.data);
    if(value == NULL)
    {
        path_string_destroy(&name);
        return clover_none(ctx);
    }
    clover_handle result = clover_string_from_utf8(ctx, value);
    path_string_destroy(&name);
    return result;
}

static clover_handle os_putenv(clover_context *ctx, clover_handle name_value,
                               clover_handle value_value)
{
    path_string name;
    path_string value;
    if(path_string_init(ctx, name_value, &name) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(path_string_init(ctx, value_value, &value) != CLOVER_STATUS_OK)
    {
        path_string_destroy(&name);
        return clover_propagate_error(ctx);
    }
    int rc = setenv(name.data, value.data, 1);
    int saved_errno = errno;
    path_string_destroy(&value);
    path_string_destroy(&name);
    if(rc != 0)
    {
        errno = saved_errno;
        return raise_errno(ctx);
    }
    return clover_none(ctx);
}

static clover_handle os_unsetenv(clover_context *ctx, clover_handle name_value)
{
    path_string name;
    if(path_string_init(ctx, name_value, &name) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    int rc = unsetenv(name.data);
    int saved_errno = errno;
    path_string_destroy(&name);
    if(rc != 0)
    {
        errno = saved_errno;
        return raise_errno(ctx);
    }
    return clover_none(ctx);
}

static clover_handle os_strerror(clover_context *ctx, clover_handle code_value)
{
    int code;
    if(value_as_int(ctx, code_value, &code) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_string_from_utf8(ctx, strerror(code));
}

static clover_handle os_system(clover_context *ctx, clover_handle command_value)
{
    path_string command;
    if(path_string_init(ctx, command_value, &command) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    int status = system(command.data);
    path_string_destroy(&command);
    return clover_int_from_int64(ctx, (int64_t)status);
}

static clover_handle os_umask(clover_context *ctx, clover_handle mask_value)
{
    int mask;
    if(value_as_int(ctx, mask_value, &mask) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    return clover_int_from_int64(ctx, (int64_t)umask((mode_t)mask));
}

static clover_handle stat_to_tuple(clover_context *ctx, const struct stat *st)
{
    clover_handle items[] = {
        clover_int_from_int64(ctx, (int64_t)st->st_mode),
        clover_int_from_int64(ctx, (int64_t)st->st_ino),
        clover_int_from_int64(ctx, (int64_t)st->st_dev),
        clover_int_from_int64(ctx, (int64_t)st->st_nlink),
        clover_int_from_int64(ctx, (int64_t)st->st_uid),
        clover_int_from_int64(ctx, (int64_t)st->st_gid),
        clover_int_from_int64(ctx, (int64_t)st->st_size),
        clover_float_from_double(ctx, (double)st->st_atime),
        clover_float_from_double(ctx, (double)st->st_mtime),
        clover_float_from_double(ctx, (double)st->st_ctime),
    };
    return clover_tuple_from_array(ctx, items, 10);
}

static clover_handle os_stat(clover_context *ctx, clover_handle path_value)
{
    path_string path;
    struct stat st;
    if(path_string_init(ctx, path_value, &path) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    int rc = stat(path.data, &st);
    int saved_errno = errno;
    path_string_destroy(&path);
    if(rc != 0)
    {
        errno = saved_errno;
        return raise_errno(ctx);
    }
    return stat_to_tuple(ctx, &st);
}

static clover_handle os_lstat(clover_context *ctx, clover_handle path_value)
{
    path_string path;
    struct stat st;
    if(path_string_init(ctx, path_value, &path) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    int rc = lstat(path.data, &st);
    int saved_errno = errno;
    path_string_destroy(&path);
    if(rc != 0)
    {
        errno = saved_errno;
        return raise_errno(ctx);
    }
    return stat_to_tuple(ctx, &st);
}

static clover_handle os_access(clover_context *ctx, clover_handle path_value,
                               clover_handle mode_value)
{
    path_string path;
    int mode;
    if(path_string_init(ctx, path_value, &path) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(value_as_int(ctx, mode_value, &mode) != CLOVER_STATUS_OK)
    {
        path_string_destroy(&path);
        return clover_propagate_error(ctx);
    }
    int rc = access(path.data, mode);
    path_string_destroy(&path);
    return clover_int_from_int64(ctx, rc == 0 ? 1 : 0);
}

static clover_handle os_chmod(clover_context *ctx, clover_handle path_value,
                              clover_handle mode_value)
{
    path_string path;
    int mode;
    if(path_string_init(ctx, path_value, &path) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(value_as_int(ctx, mode_value, &mode) != CLOVER_STATUS_OK)
    {
        path_string_destroy(&path);
        return clover_propagate_error(ctx);
    }
    int rc = chmod(path.data, (mode_t)mode);
    int saved_errno = errno;
    path_string_destroy(&path);
    if(rc != 0)
    {
        errno = saved_errno;
        return raise_errno(ctx);
    }
    return clover_none(ctx);
}

static clover_handle os_mkdir(clover_context *ctx, clover_handle path_value,
                              clover_handle mode_value)
{
    path_string path;
    int mode;
    if(path_string_init(ctx, path_value, &path) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(value_as_int(ctx, mode_value, &mode) != CLOVER_STATUS_OK)
    {
        path_string_destroy(&path);
        return clover_propagate_error(ctx);
    }
    int rc = mkdir(path.data, (mode_t)mode);
    int saved_errno = errno;
    path_string_destroy(&path);
    if(rc != 0)
    {
        errno = saved_errno;
        return raise_errno(ctx);
    }
    return clover_none(ctx);
}

static clover_handle os_rmdir(clover_context *ctx, clover_handle path_value)
{
    path_string path;
    if(path_string_init(ctx, path_value, &path) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    int rc = rmdir(path.data);
    int saved_errno = errno;
    path_string_destroy(&path);
    if(rc != 0)
    {
        errno = saved_errno;
        return raise_errno(ctx);
    }
    return clover_none(ctx);
}

static clover_handle os_unlink(clover_context *ctx, clover_handle path_value)
{
    path_string path;
    if(path_string_init(ctx, path_value, &path) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    int rc = unlink(path.data);
    int saved_errno = errno;
    path_string_destroy(&path);
    if(rc != 0)
    {
        errno = saved_errno;
        return raise_errno(ctx);
    }
    return clover_none(ctx);
}

static clover_handle os_rename(clover_context *ctx, clover_handle src_value,
                               clover_handle dst_value)
{
    path_string src;
    path_string dst;
    if(path_string_init(ctx, src_value, &src) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }
    if(path_string_init(ctx, dst_value, &dst) != CLOVER_STATUS_OK)
    {
        path_string_destroy(&src);
        return clover_propagate_error(ctx);
    }
    int rc = rename(src.data, dst.data);
    int saved_errno = errno;
    path_string_destroy(&dst);
    path_string_destroy(&src);
    if(rc != 0)
    {
        errno = saved_errno;
        return raise_errno(ctx);
    }
    return clover_none(ctx);
}

static clover_handle os_path_split(clover_context *ctx,
                                   clover_handle path_value)
{
    path_string path;
    if(path_string_init(ctx, path_value, &path) != CLOVER_STATUS_OK)
    {
        return clover_propagate_error(ctx);
    }

    char *last_sep = strrchr(path.data, '/');
    if(last_sep == NULL)
    {
        clover_handle result =
            clover_tuple_from_pair(ctx, clover_string_from_utf8(ctx, ""),
                                   clover_string_from_utf8(ctx, path.data));
        path_string_destroy(&path);
        return result;
    }

    size_t head_size = (size_t)(last_sep - path.data);
    const char *tail = last_sep + 1;
    if(head_size == 0)
    {
        clover_handle result =
            clover_tuple_from_pair(ctx, clover_string_from_utf8(ctx, "/"),
                                   clover_string_from_utf8(ctx, tail));
        path_string_destroy(&path);
        return result;
    }

    *last_sep = '\0';
    clover_handle result =
        clover_tuple_from_pair(ctx, clover_string_from_utf8(ctx, path.data),
                               clover_string_from_utf8(ctx, tail));
    path_string_destroy(&path);
    return result;
}

#define ADD_FUNCTION_0(name, fn, doc)                                          \
    do                                                                         \
    {                                                                          \
        if(clover_module_add_function_0(builder, name, fn, doc) !=             \
           CLOVER_STATUS_OK)                                                   \
        {                                                                      \
            return CLOVER_STATUS_ERROR;                                        \
        }                                                                      \
    }                                                                          \
    while(0)

#define ADD_FUNCTION_1(name, fn, doc)                                          \
    do                                                                         \
    {                                                                          \
        if(clover_module_add_function_1(builder, name, fn, doc) !=             \
           CLOVER_STATUS_OK)                                                   \
        {                                                                      \
            return CLOVER_STATUS_ERROR;                                        \
        }                                                                      \
    }                                                                          \
    while(0)

#define ADD_FUNCTION_2(name, fn, doc)                                          \
    do                                                                         \
    {                                                                          \
        if(clover_module_add_function_2(builder, name, fn, doc) !=             \
           CLOVER_STATUS_OK)                                                   \
        {                                                                      \
            return CLOVER_STATUS_ERROR;                                        \
        }                                                                      \
    }                                                                          \
    while(0)

#define ADD_INT(name, value)                                                   \
    do                                                                         \
    {                                                                          \
        if(clover_module_add_value(builder, name,                              \
                                   clover_int_from_int64(ctx, value)) !=       \
           CLOVER_STATUS_OK)                                                   \
        {                                                                      \
            return CLOVER_STATUS_ERROR;                                        \
        }                                                                      \
    }                                                                          \
    while(0)

CL_NATIVE_MODULE_EXPORT
clover_status clover_module_init__os(clover_context *ctx,
                                     clover_native_module_builder *builder)
{
    ADD_FUNCTION_0("getcwd", os_getcwd,
                   "Return the current working directory.");
    ADD_FUNCTION_0("getpid", os_getpid, "Return the current process id.");
    ADD_FUNCTION_0("getppid", os_getppid, "Return the parent process id.");
    ADD_FUNCTION_0("getuid", os_getuid, "Return the current user id.");
    ADD_FUNCTION_0("geteuid", os_geteuid,
                   "Return the current effective user id.");
    ADD_FUNCTION_0("getgid", os_getgid, "Return the current group id.");
    ADD_FUNCTION_0("getegid", os_getegid,
                   "Return the current effective group id.");
    ADD_FUNCTION_1("chdir", os_chdir, "Change the current working directory.");
    ADD_FUNCTION_1("listdir", os_listdir, "Return directory entries.");
    ADD_FUNCTION_1("getenv", os_getenv, "Return an environment variable.");
    ADD_FUNCTION_1("unsetenv", os_unsetenv, "Unset an environment variable.");
    ADD_FUNCTION_1("strerror", os_strerror, "Return an errno message.");
    ADD_FUNCTION_1("system", os_system, "Execute a shell command.");
    ADD_FUNCTION_1("umask", os_umask, "Set and return the process umask.");
    ADD_FUNCTION_1("stat", os_stat, "Return a stat tuple.");
    ADD_FUNCTION_1("lstat", os_lstat, "Return an lstat tuple.");
    ADD_FUNCTION_1("rmdir", os_rmdir, "Remove a directory.");
    ADD_FUNCTION_1("unlink", os_unlink, "Remove a file.");
    ADD_FUNCTION_2("putenv", os_putenv, "Set an environment variable.");
    ADD_FUNCTION_2("access", os_access, "Test path accessibility.");
    ADD_FUNCTION_2("chmod", os_chmod, "Change path mode bits.");
    ADD_FUNCTION_2("mkdir", os_mkdir, "Create a directory.");
    ADD_FUNCTION_2("rename", os_rename, "Rename a path.");
    ADD_FUNCTION_1("_path_split", os_path_split, "Split a POSIX path.");

    ADD_INT("F_OK", F_OK);
    ADD_INT("R_OK", R_OK);
    ADD_INT("W_OK", W_OK);
    ADD_INT("X_OK", X_OK);
    ADD_INT("SEEK_SET", SEEK_SET);
    ADD_INT("SEEK_CUR", SEEK_CUR);
    ADD_INT("SEEK_END", SEEK_END);
    ADD_INT("O_RDONLY", O_RDONLY);
    ADD_INT("O_WRONLY", O_WRONLY);
    ADD_INT("O_RDWR", O_RDWR);
    ADD_INT("O_CREAT", O_CREAT);
    ADD_INT("O_EXCL", O_EXCL);
    ADD_INT("O_TRUNC", O_TRUNC);
    ADD_INT("O_APPEND", O_APPEND);

    return CLOVER_STATUS_OK;
}
