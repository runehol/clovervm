#include "import_system.h"

#include "list.h"
#include "module_object.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
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

        std::optional<ModuleSpec>
        find_source_module_spec_in_path_entry(const std::wstring &module_name,
                                              Value path_entry)
        {
            if(!can_convert_to<String>(path_entry))
            {
                return std::nullopt;
            }

            std::filesystem::path base(string_to_wstring(
                TValue<String>::from_value_assumed(path_entry)));
            std::filesystem::path package_dir = base / module_name;
            std::filesystem::path package_init = package_dir / L"__init__.py";
            if(file_exists(package_init))
            {
                return ModuleSpec{
                    module_name,
                    absolute_wstring_path(package_init),
                    true,
                    {absolute_wstring_path(package_dir)},
                };
            }

            std::filesystem::path module_file = base / (module_name + L".py");
            if(file_exists(module_file))
            {
                return ModuleSpec{
                    module_name,
                    absolute_wstring_path(module_file),
                    false,
                    {},
                };
            }

            return std::nullopt;
        }
    }  // namespace

    std::optional<ModuleSpec> find_source_module_spec(ThreadState *thread,
                                                      TValue<String> name)
    {
        std::wstring module_name = string_to_wstring(name);
        if(module_name.empty() || module_name.find(L'.') != std::wstring::npos)
        {
            return std::nullopt;
        }

        TValue<String> path_name =
            thread->get_machine()->get_or_create_interned_string_value(L"path");
        Value path_value =
            thread->get_machine()->sys_module().extract()->get_own_property(
                path_name);
        if(!can_convert_to<List>(path_value))
        {
            return std::nullopt;
        }

        List *path = path_value.get_ptr<List>();
        for(size_t path_idx = 0; path_idx < path->size(); ++path_idx)
        {
            std::optional<ModuleSpec> spec =
                find_source_module_spec_in_path_entry(
                    module_name, path->item_unchecked(path_idx));
            if(spec.has_value())
            {
                return spec;
            }
        }

        return std::nullopt;
    }

}  // namespace cl
