
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



void op_add(StackFrame *frame, uint8_t *pc, CLValue accumulator, void *dispatch, CodeObject *code_object)
{
  auto *dispatch_fun = reinterpret_cast<DispatchTable>(dispatch)[pc[2]];
  CLValue a = frame->registers[pc[1]];
  if(unlikely(!(((a.v|accumulator.v)& cl_tag_mask)==0)))
  {
    __attribute__((musttail)) return slow_path(ARGS);
  }
  if(unlikely(__builtin_saddll_overflow(a.v, accumulator.v, &accumulator.v)))
  {
    __attribute__((musttail)) return overflow_path(ARGS);
  }

  pc += 2;
  dispatch_fun(ARGS);
}


void op_sub(StackFrame *frame, uint8_t *pc, CLValue accumulator, void *dispatch, CodeObject *code_object)
{
  auto *dispatch_fun = reinterpret_cast<DispatchTable>(dispatch)[pc[2]];
  CLValue a = frame->registers[pc[1]];
  if(unlikely(!(((a.v|accumulator.v)& cl_tag_mask)==0)))
  {
    __attribute__((musttail)) return slow_path(ARGS);
  }
  if(unlikely(__builtin_ssubll_overflow(a.v, accumulator.v, &accumulator.v)))
  {
    __attribute__((musttail)) return overflow_path(ARGS);
  }

  pc += 2;
  dispatch_fun(ARGS);
}


void op_mul(StackFrame *frame, uint8_t *pc, CLValue accumulator, void *dispatch, CodeObject *code_object)
{
  auto *dispatch_fun = reinterpret_cast<DispatchTable>(dispatch)[pc[2]];
  CLValue a = frame->registers[pc[1]];
  if(unlikely(!(((a.v|accumulator.v)& cl_tag_mask)==0)))
  {
    __attribute__((musttail)) return slow_path(ARGS);
  }
  if(unlikely(__builtin_smulll_overflow(a.v, (accumulator.v>>cl_tag_bits), &accumulator.v)))
  {
    __attribute__((musttail)) return overflow_path(ARGS);
  }

  pc += 2;
  dispatch_fun(ARGS);
}


void op_sub_smi(StackFrame *frame, uint8_t *pc, CLValue accumulator, void *dispatch, CodeObject *code_object)
{
  auto *dispatch_fun = reinterpret_cast<DispatchTable>(dispatch)[pc[2]];
  int8_t smi = pc[1];
  if(unlikely(!((accumulator.v& cl_tag_mask)==0)))
  {
    __attribute__((musttail)) return slow_path(ARGS);
  }
  if(unlikely(__builtin_ssubll_overflow(accumulator.v, (smi<<cl_tag_bits), &accumulator.v)))
  {
    __attribute__((musttail)) return overflow_path(ARGS);
  }

  pc += 2;
  dispatch_fun(ARGS);
}


void op_add_smi(StackFrame *frame, uint8_t *pc, CLValue accumulator, void *dispatch, CodeObject *code_object)
{
  auto *dispatch_fun = reinterpret_cast<DispatchTable>(dispatch)[pc[2]];
  int8_t smi = pc[1];
  if(unlikely(!((accumulator.v& cl_tag_mask)==0)))
  {
    __attribute__((musttail)) return slow_path(ARGS);
  }
  if(unlikely(__builtin_saddll_overflow(accumulator.v, (smi<<cl_tag_bits), &accumulator.v)))
  {
    __attribute__((musttail)) return overflow_path(ARGS);
  }

  pc += 2;
  dispatch_fun(ARGS);
}


void op_mul_smi(StackFrame *frame, uint8_t *pc, CLValue accumulator, void *dispatch, CodeObject *code_object)
{
  auto *dispatch_fun = reinterpret_cast<DispatchTable>(dispatch)[pc[2]];
  int8_t smi = pc[1];
  if(unlikely(!((accumulator.v& cl_tag_mask)==0)))
  {
    __attribute__((musttail)) return slow_path(ARGS);
  }
  if(unlikely(__builtin_smulll_overflow(accumulator.v, smi, &accumulator.v)))
  {
    __attribute__((musttail)) return overflow_path(ARGS);
  }

  pc += 2;
  dispatch_fun(ARGS);
}


void op_neg(StackFrame *frame, uint8_t *pc, CLValue accumulator, void *dispatch, CodeObject *code_object)
{
  auto *dispatch_fun = reinterpret_cast<DispatchTable>(dispatch)[pc[1]];
  
  if(unlikely(!((accumulator.v& cl_tag_mask)==0)))
  {
    __attribute__((musttail)) return slow_path(ARGS);
  }
  if(unlikely(__builtin_ssubll_overflow(0, accumulator.v, &accumulator.v)))
  {
    __attribute__((musttail)) return overflow_path(ARGS);
  }

  pc += 1;
  dispatch_fun(ARGS);
}

void (*dispatch_table[])(PARAMS) = {
  op_add,
  op_sub,
  op_mul,
  op_sub_smi,
  op_add_smi,
  op_mul_smi,
  op_neg,

};
}

