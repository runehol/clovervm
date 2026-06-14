#include "cpp_benchmarks.h"

#include <array>
#include <string>

namespace benchmark_cpp
{
    namespace
    {
        constexpr int64_t ident1 = 1;
        constexpr int64_t ident2 = 2;
        constexpr int64_t ident3 = 3;
        constexpr int64_t ident4 = 4;
        constexpr int64_t ident5 = 5;

        struct PystoneRecord
        {
            PystoneRecord *ptr_comp = nullptr;
            int64_t discr = 0;
            int64_t enum_comp = 0;
            int64_t int_comp = 0;
            const char *string_comp = nullptr;
        };

        struct PystoneContext
        {
            int64_t int_glob = 0;
            bool bool_glob = false;
            char char1_glob = '\0';
            char char2_glob = '\0';
            std::array<int64_t, 51> array1_glob{};
            std::array<std::array<int64_t, 51>, 51> array2_glob{};
            PystoneRecord ptr_glb;
            PystoneRecord ptr_glb_next;
            PystoneRecord proc1_next_record;
        };

        PystoneRecord pystone_copy_record(const PystoneRecord &record)
        {
            return record;
        }

        bool pystone_func3(int64_t enum_par_in)
        {
            int64_t enum_loc = enum_par_in;
            return enum_loc == ident3;
        }

        int64_t pystone_func1(char char_par1, char char_par2)
        {
            char char_loc1 = char_par1;
            char char_loc2 = char_loc1;
            if(char_loc2 != char_par2)
            {
                return ident1;
            }
            return ident2;
        }

        bool pystone_func2(const std::string &str_par_i1,
                           const std::string &str_par_i2)
        {
            int64_t int_loc = 1;
            char char_loc = '\0';
            while(int_loc <= 1)
            {
                if(pystone_func1(str_par_i1[int_loc],
                                 str_par_i2[int_loc + 1]) == ident1)
                {
                    char_loc = 'A';
                    int_loc = int_loc + 1;
                }
            }
            if(char_loc >= 'W' && char_loc <= 'Z')
            {
                int_loc = 7;
            }
            if(char_loc == 'X')
            {
                return true;
            }
            if(str_par_i1 > str_par_i2)
            {
                int_loc = int_loc + 7;
                (void)int_loc;
                return true;
            }
            return false;
        }

        int64_t pystone_proc6(const PystoneContext &ctx, int64_t enum_par_in)
        {
            int64_t enum_par_out = enum_par_in;
            if(!pystone_func3(enum_par_in))
            {
                enum_par_out = ident4;
            }
            if(enum_par_in == ident1)
            {
                enum_par_out = ident1;
            }
            else if(enum_par_in == ident2)
            {
                if(ctx.int_glob > 100)
                {
                    enum_par_out = ident1;
                }
                else
                {
                    enum_par_out = ident4;
                }
            }
            else if(enum_par_in == ident3)
            {
                enum_par_out = ident2;
            }
            else if(enum_par_in == ident4)
            {
            }
            else if(enum_par_in == ident5)
            {
                enum_par_out = ident3;
            }
            return enum_par_out;
        }

        int64_t pystone_proc7(int64_t int_par_i1, int64_t int_par_i2)
        {
            int64_t int_loc = int_par_i1 + 2;
            int64_t int_par_out = int_par_i2 + int_loc;
            return int_par_out;
        }

        PystoneRecord *pystone_proc3(PystoneContext &ctx,
                                     PystoneRecord *ptr_par_out)
        {
            if(ctx.ptr_glb.ptr_comp != nullptr)
            {
                ptr_par_out = ctx.ptr_glb.ptr_comp;
            }
            else
            {
                ctx.int_glob = 100;
            }
            ctx.ptr_glb.int_comp = pystone_proc7(10, ctx.int_glob);
            return ptr_par_out;
        }

        PystoneRecord *pystone_proc1(PystoneContext &ctx,
                                     PystoneRecord *ptr_par_in)
        {
            ctx.proc1_next_record = pystone_copy_record(ctx.ptr_glb);
            PystoneRecord &next_record = ctx.proc1_next_record;
            ptr_par_in->ptr_comp = &ctx.proc1_next_record;
            ptr_par_in->int_comp = 5;
            next_record.int_comp = ptr_par_in->int_comp;
            next_record.ptr_comp = ptr_par_in->ptr_comp;
            next_record.ptr_comp = pystone_proc3(ctx, next_record.ptr_comp);
            if(next_record.discr == ident1)
            {
                next_record.int_comp = 6;
                next_record.enum_comp =
                    pystone_proc6(ctx, ptr_par_in->enum_comp);
                next_record.ptr_comp = ctx.ptr_glb.ptr_comp;
                next_record.int_comp = pystone_proc7(next_record.int_comp, 10);
            }
            else
            {
                *ptr_par_in = pystone_copy_record(next_record);
            }
            next_record.ptr_comp = nullptr;
            return ptr_par_in;
        }

        int64_t pystone_proc2(const PystoneContext &ctx, int64_t int_par_io)
        {
            int64_t int_loc = int_par_io + 10;
            int64_t enum_loc = 0;
            while(true)
            {
                if(ctx.char1_glob == 'A')
                {
                    int_loc = int_loc - 1;
                    int_par_io = int_loc - ctx.int_glob;
                    enum_loc = ident1;
                }
                if(enum_loc == ident1)
                {
                    break;
                }
            }
            return int_par_io;
        }

        void pystone_proc4(PystoneContext &ctx)
        {
            bool bool_loc = ctx.char1_glob == 'A';
            bool_loc = bool_loc || ctx.bool_glob;
            (void)bool_loc;
            ctx.char2_glob = 'B';
        }

        void pystone_proc5(PystoneContext &ctx)
        {
            ctx.char1_glob = 'A';
            ctx.bool_glob = false;
        }

        void pystone_proc8(PystoneContext &ctx,
                           std::array<int64_t, 51> &array1_par,
                           std::array<std::array<int64_t, 51>, 51> &array2_par,
                           int64_t int_par_i1, int64_t int_par_i2)
        {
            int64_t int_loc = int_par_i1 + 5;
            array1_par[int_loc] = int_par_i2;
            array1_par[int_loc + 1] = array1_par[int_loc];
            array1_par[int_loc + 30] = int_loc;
            for(int64_t int_index = int_loc; int_index < int_loc + 2;
                ++int_index)
            {
                array2_par[int_loc][int_index] = int_loc;
            }
            array2_par[int_loc][int_loc - 1] =
                array2_par[int_loc][int_loc - 1] + 1;
            array2_par[int_loc + 20][int_loc] = array1_par[int_loc];
            ctx.int_glob = 5;
        }

        int64_t pystone_checksum(const PystoneContext &ctx)
        {
            int64_t result = ctx.int_glob;
            if(ctx.bool_glob)
            {
                result += 1;
            }
            if(ctx.char1_glob == 'A')
            {
                result += 10;
            }
            if(ctx.char2_glob == 'B')
            {
                result += 100;
            }
            result += ctx.array1_glob[8];
            result += ctx.array1_glob[9];
            result += ctx.array1_glob[38];
            result += ctx.array2_glob[8][7];
            result += ctx.array2_glob[8][8];
            result += ctx.array2_glob[28][8];
            result += ctx.ptr_glb.discr;
            result += ctx.ptr_glb.enum_comp;
            result += ctx.ptr_glb.int_comp;
            result += ctx.ptr_glb.ptr_comp->int_comp;
            return result;
        }
    }  // namespace

    int64_t pystone_run(int64_t n)
    {
        PystoneContext ctx;
        ctx.ptr_glb_next = {nullptr, 0, 0, 0, nullptr};
        ctx.ptr_glb = {&ctx.ptr_glb_next, ident1, ident3, 40,
                       "DHRYSTONE PROGRAM, SOME STRING"};
        std::string string1_loc = "DHRYSTONE PROGRAM, 1'ST STRING";
        ctx.array2_glob[8][7] = 10;

        for(int64_t i = 0; i < n; ++i)
        {
            pystone_proc5(ctx);
            pystone_proc4(ctx);
            int64_t int_loc1 = 2;
            int64_t int_loc2 = 3;
            std::string string2_loc = "DHRYSTONE PROGRAM, 2'ND STRING";
            int64_t enum_loc = ident2;
            ctx.bool_glob = !pystone_func2(string1_loc, string2_loc);
            int64_t int_loc3 = 0;
            while(int_loc1 < int_loc2)
            {
                int_loc3 = 5 * int_loc1 - int_loc2;
                int_loc3 = pystone_proc7(int_loc1, int_loc2);
                int_loc1 = int_loc1 + 1;
            }
            pystone_proc8(ctx, ctx.array1_glob, ctx.array2_glob, int_loc1,
                          int_loc3);
            ctx.ptr_glb = *pystone_proc1(ctx, &ctx.ptr_glb);
            char char_index = 'A';
            while(char_index <= ctx.char2_glob)
            {
                if(enum_loc == pystone_func1(char_index, 'C'))
                {
                    enum_loc = pystone_proc6(ctx, ident1);
                }
                char_index = static_cast<char>(char_index + 1);
            }
            int_loc3 = int_loc2 * int_loc1;
            double int_loc2_float =
                static_cast<double>(int_loc3) / static_cast<double>(int_loc1);
            int_loc2_float = 7 * (int_loc3 - int_loc2_float) - int_loc1;
            (void)int_loc2_float;
            int_loc1 = pystone_proc2(ctx, int_loc1);
            (void)int_loc1;
        }

        return pystone_checksum(ctx);
    }

    int64_t pystone_items(int64_t n) { return n; }
}  // namespace benchmark_cpp
