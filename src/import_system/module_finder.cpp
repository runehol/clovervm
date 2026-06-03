#include "import_system/module_finder.h"

#include "build_config.h"
#include "builtin_types/list.h"
#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <filesystem>
#include <system_error>

namespace cl
{
    namespace
    {
        std::wstring string_to_wstring(TValue<String> string)
        {
            return string_as_wchar_t(string);
        }

        bool file_exists(const std::filesystem::path &path)
        {
            std::error_code ec;
            return std::filesystem::is_regular_file(path, ec);
        }

        bool directory_exists(const std::filesystem::path &path)
        {
            std::error_code ec;
            return std::filesystem::is_directory(path, ec);
        }

        std::wstring absolute_wstring_path(const std::filesystem::path &path)
        {
            std::error_code ec;
            std::filesystem::path absolute_path =
                std::filesystem::absolute(path, ec);
            if(ec)
            {
                absolute_path = path;
            }
            return absolute_path.lexically_normal().wstring();
        }

        bool path_is_at_or_under(const std::wstring &path,
                                 const std::wstring &root)
        {
            if(path == root)
            {
                return true;
            }
            std::wstring root_prefix = root;
            if(!root_prefix.empty() &&
               root_prefix.back() != std::filesystem::path::preferred_separator)
            {
                root_prefix += std::filesystem::path::preferred_separator;
            }
            return path.rfind(root_prefix, 0) == 0;
        }

        bool is_stdlib_path_entry(const std::filesystem::path &base)
        {
            std::wstring absolute_base = absolute_wstring_path(base);
            return path_is_at_or_under(
                       absolute_base,
                       absolute_wstring_path(
                           std::filesystem::path(CL_BUILD_STDLIB_DIR))) ||
                   path_is_at_or_under(absolute_base, absolute_wstring_path(
                                                          std::filesystem::path(
                                                              CL_STDLIB_DIR)));
        }

        std::optional<ModuleSpec>
        find_source_module_spec_in_path_entry(const std::wstring &full_name,
                                              const std::wstring &leaf_name,
                                              Value path_entry)
        {
            if(!can_convert_to<String>(path_entry))
            {
                return std::nullopt;
            }

            std::filesystem::path base(string_to_wstring(
                TValue<String>::from_value_assumed(path_entry)));
            if(base.empty())
            {
                base = L".";
            }
            bool trusted_source = is_stdlib_path_entry(base);
            std::filesystem::path package_dir = base / leaf_name;
            std::filesystem::path package_init = package_dir / L"__init__.py";
            if(file_exists(package_init))
            {
                return ModuleSpec{
                    ModuleSpecKind::Source,
                    full_name,
                    absolute_wstring_path(package_init),
                    true,
                    trusted_source,
                    {absolute_wstring_path(package_dir)},
                };
            }

            std::filesystem::path module_file = base / (leaf_name + L".py");
            if(file_exists(module_file))
            {
                return ModuleSpec{
                    ModuleSpecKind::Source,
                    full_name,
                    absolute_wstring_path(module_file),
                    false,
                    trusted_source,
                    {},
                };
            }

            std::filesystem::path native_module_file =
                base / (leaf_name + CL_NATIVE_MODULE_SUFFIX);
            if(file_exists(native_module_file))
            {
                return ModuleSpec{
                    ModuleSpecKind::NativeExtension,
                    full_name,
                    absolute_wstring_path(native_module_file),
                    false,
                    false,
                    {},
                };
            }

            if(directory_exists(package_dir))
            {
                return ModuleSpec{
                    ModuleSpecKind::Namespace,
                    full_name,
                    L"namespace",
                    true,
                    false,
                    {absolute_wstring_path(package_dir)},
                };
            }

            return std::nullopt;
        }

        TValue<String> interned_string(ThreadState *thread, const wchar_t *text)
        {
            return thread->get_machine()->get_or_create_interned_string_value(
                text);
        }

        std::optional<ModuleSpec>
        find_builtin_module_spec(const std::wstring &full_name)
        {
            if(full_name == L"sys" || full_name == L"builtins")
            {
                return ModuleSpec{
                    ModuleSpecKind::Builtin,
                    full_name,
                    L"built-in",
                    false,
                    false,
                    {},
                };
            }
            return std::nullopt;
        }

        std::optional<ModuleSpec> find_source_module_spec_on_path(
            ThreadState *thread, const std::wstring &full_name,
            const std::wstring &leaf_name, List *path)
        {
            (void)thread;
            for(size_t path_idx = 0; path_idx < path->size(); ++path_idx)
            {
                std::optional<ModuleSpec> spec =
                    find_source_module_spec_in_path_entry(
                        full_name, leaf_name, path->item_unchecked(path_idx));
                if(spec.has_value())
                {
                    return spec;
                }
            }

            return std::nullopt;
        }
    }  // namespace

    List *sys_path(ThreadState *thread)
    {
        TValue<String> path_name = interned_string(thread, L"path");
        Value path_value =
            thread->get_machine()->sys_module().extract()->get_own_property(
                path_name);
        if(!can_convert_to<List>(path_value))
        {
            return nullptr;
        }
        return path_value.get_ptr<List>();
    }

    std::optional<ModuleSpec> find_module_spec(ThreadState *thread,
                                               const std::wstring &full_name,
                                               const std::wstring &leaf_name,
                                               List *path)
    {
        std::optional<ModuleSpec> builtin_spec =
            find_builtin_module_spec(full_name);
        if(builtin_spec.has_value())
        {
            return builtin_spec;
        }

        if(path == nullptr)
        {
            return std::nullopt;
        }

        return find_source_module_spec_on_path(thread, full_name, leaf_name,
                                               path);
    }

    std::optional<ModuleSpec> find_source_module_spec(ThreadState *thread,
                                                      TValue<String> name)
    {
        std::wstring module_name = string_to_wstring(name);
        if(module_name.empty() || module_name.find(L'.') != std::wstring::npos)
        {
            return std::nullopt;
        }

        List *path = sys_path(thread);
        if(path == nullptr)
        {
            return std::nullopt;
        }

        return find_source_module_spec_on_path(thread, module_name, module_name,
                                               path);
    }

}  // namespace cl
