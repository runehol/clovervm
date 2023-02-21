#!/usr/bin/env python3

from io import StringIO
from jinja2 import Environment, PackageLoader, select_autoescape


base_env = Environment(autoescape=False)

class InstructionClass:

    def __init__(self, name, instr_len, variables):
        self.name = name
        self.instr_len = instr_len
        self.variables = dict(variables)
        self.variables["preamble1"] = """
void op_"""
        self.variables["preamble2"] = """(StackFrame *frame, uint8_t *pc, CLValue accumulator, void *dispatch, CodeObject *code_object)
{
  auto *dispatch_fun = reinterpret_cast<DispatchTable>(dispatch)[pc[%s]];
  %s""" % (self.instr_len, self.variables["init_operands"])

        self.variables["postamble"] = """
  pc += %s;
  dispatch_fun(ARGS);
}
""" % (self.instr_len,)




binary_arithmetic_reg_acc = InstructionClass("binary_arithmetic_reg_acc", 2,
                                  dict(all_operands_smi = "(((a.v|accumulator.v)& cl_tag_mask)==0)",
                                       a = "a",
                                       b = "accumulator",
                                       dest = "accumulator",
                                       a_as_encoded_smi = "a.v",
                                       a_as_int = "(a.v>>cl_tag_bits)",
                                       b_as_encoded_smi = "accumulator.v",
                                       b_as_int = "(accumulator.v>>cl_tag_bits)",
                                       dest_as_encoded_smi = "accumulator.v",
                                       do_slow_path = "__attribute__((musttail)) return slow_path(ARGS);",
                                       do_overflow_path = "__attribute__((musttail)) return overflow_path(ARGS);",
                                       init_operands = "CLValue a = frame->registers[pc[1]];",
                                        ))

binary_arithmetic_acc_smi = InstructionClass("binary_arithmetic_acc_smi", 2,
                                  dict(all_operands_smi = "((accumulator.v& cl_tag_mask)==0)",
                                       a = "accumulator",
                                       b = "value_make_smi(smi)",
                                       dest = "accumulator",
                                       a_as_encoded_smi = "accumulator.v",
                                       a_as_int = "(accumulator.v>>cl_tag_bits)",
                                       b_as_encoded_smi = "(smi<<cl_tag_bits)",
                                       b_as_int = "smi",
                                       dest_as_encoded_smi = "accumulator.v",
                                       do_slow_path = "__attribute__((musttail)) return slow_path(ARGS);",
                                       do_overflow_path = "__attribute__((musttail)) return overflow_path(ARGS);",
                                       init_operands = "int8_t smi = pc[1];",
                                        ))


unary_arithmetic_acc = InstructionClass("acc", 1,
                                  dict(all_operands_smi = "((accumulator.v& cl_tag_mask)==0)",
                                       a = "accumulator",
                                       dest = "accumulator",
                                       a_as_encoded_smi = "accumulator.v",
                                       a_as_int = "(accumulator.v>>cl_tag_bits)",
                                       dest_as_encoded_smi = "accumulator.v",
                                       do_slow_path = "__attribute__((musttail)) return slow_path(ARGS);",
                                       do_overflow_path = "__attribute__((musttail)) return overflow_path(ARGS);",
                                       init_operands = "",
                                        ))



class Operation:
    def __init__(self, name, template):
        self.template = template


op_add = Operation("op_add", """
{{preamble1}}{{opcode_name}}{{preamble2}}
  if(unlikely(!{{all_operands_smi}}))
  {
    {{do_slow_path}}
  }
  if(unlikely(__builtin_saddll_overflow({{a_as_encoded_smi}}, {{b_as_encoded_smi}}, &{{dest_as_encoded_smi}})))
  {
    {{do_overflow_path}}
  }
{{postamble}}
""")


op_sub = Operation("op_sub", """
{{preamble1}}{{opcode_name}}{{preamble2}}
  if(unlikely(!{{all_operands_smi}}))
  {
    {{do_slow_path}}
  }
  if(unlikely(__builtin_ssubll_overflow({{a_as_encoded_smi}}, {{b_as_encoded_smi}}, &{{dest_as_encoded_smi}})))
  {
    {{do_overflow_path}}
  }
{{postamble}}
""")


op_mul = Operation("op_mul", """
{{preamble1}}{{opcode_name}}{{preamble2}}
  if(unlikely(!{{all_operands_smi}}))
  {
    {{do_slow_path}}
  }
  if(unlikely(__builtin_smulll_overflow({{a_as_encoded_smi}}, {{b_as_int}}, &{{dest_as_encoded_smi}})))
  {
    {{do_overflow_path}}
  }
{{postamble}}
""")

op_neg = Operation("op_neg", """
{{preamble1}}{{opcode_name}}{{preamble2}}
  if(unlikely(!{{all_operands_smi}}))
  {
    {{do_slow_path}}
  }
  if(unlikely(__builtin_ssubll_overflow(0, {{a_as_encoded_smi}}, &{{dest_as_encoded_smi}})))
  {
    {{do_overflow_path}}
  }
{{postamble}}
""")


class Opcode:
    def __init__(self, name, operation, instruction_class):
        self.name = name
        self.instruction_class = instruction_class
        self.operation = operation


    def render(self):
        template = base_env.from_string(source=self.operation.template, globals=self.instruction_class.variables)
        return template.render(opcode_name = self.name)


opcodes = [
    Opcode("add", op_add, binary_arithmetic_reg_acc),
    Opcode("sub", op_sub, binary_arithmetic_reg_acc),
    Opcode("mul", op_mul, binary_arithmetic_reg_acc),
    Opcode("sub_smi", op_sub, binary_arithmetic_acc_smi),
    Opcode("add_smi", op_add, binary_arithmetic_acc_smi),
    Opcode("mul_smi", op_mul, binary_arithmetic_acc_smi),
    Opcode("neg", op_neg, unary_arithmetic_acc),
    ]


def gen_interpreter():
    f = StringIO()

    f.write("""
#include "cl_value.h"
namespace cl {
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)


struct StackFrame
{
      CLValue registers[100];
};

struct CodeObject
{

};

#define PARAMS StackFrame *frame, uint8_t *pc, CLValue accumulator, void *dispatch, CodeObject *code_object
#define ARGS frame, pc, accumulator, dispatch, code_object

extern void (*dispatch_table[])(PARAMS);

using DispatchTable = decltype(&dispatch_table[0]);



void slow_path(PARAMS);
void overflow_path(PARAMS);

""")
    for opcode in opcodes:
        f.write(opcode.render())

    f.write("""
void (*dispatch_table[])(PARAMS) = {
""")

    for opcode in opcodes:
        f.write(f"  op_{opcode.name},\n")

    f.write("""
};
}
""")
    return f.getvalue()


print(gen_interpreter())
