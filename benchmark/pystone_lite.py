class Record:
    pass


class Context:
    pass


def make_record(ptr_comp, discr, enum_comp, int_comp):
    record = Record()
    record.ptr_comp = ptr_comp
    record.discr = discr
    record.enum_comp = enum_comp
    record.int_comp = int_comp
    return record


def copy_record(src, dst):
    dst.ptr_comp = src.ptr_comp
    dst.discr = src.discr
    dst.enum_comp = src.enum_comp
    dst.int_comp = src.int_comp


def proc6(enum_par_in):
    if enum_par_in == 1:
        return 2
    if enum_par_in == 2:
        return 3
    if enum_par_in == 3:
        return 1
    return 1


def proc7(int_par_i1, int_par_i2):
    return int_par_i2 + int_par_i1 + 2


def proc3(ctx, ptr_par_out):
    if ptr_par_out is not None:
        ctx.int_glob = proc7(10, ctx.int_glob)
        return ptr_par_out.ptr_comp
    return ctx.ptr_glb


def proc2(ctx, int_par_io):
    int_loc = int_par_io + 10
    while int_loc > 3:
        int_loc -= 1
        ctx.int_glob += int_loc
    return int_par_io + ctx.int_glob


def proc1(ctx, ptr_par_in):
    next_record = ptr_par_in.ptr_comp
    copy_record(ctx.record_template, next_record)
    ptr_par_in.int_comp = 5
    next_record.int_comp = ptr_par_in.int_comp
    next_record.ptr_comp = ptr_par_in.ptr_comp
    next_record.ptr_comp = proc3(ctx, next_record.ptr_comp)
    if next_record.discr == 1:
        next_record.int_comp = 6
        next_record.enum_comp = proc6(ptr_par_in.enum_comp)
        next_record.int_comp = proc7(next_record.int_comp, 10)
    else:
        copy_record(next_record, ptr_par_in)
    return ptr_par_in.int_comp + next_record.int_comp + next_record.enum_comp


def run(n):
    ctx = Context()
    ctx.int_glob = 0
    ctx.ptr_glb_next = make_record(None, 1, 3, 0)
    ctx.ptr_glb = make_record(ctx.ptr_glb_next, 1, 3, 40)
    ctx.record_template = make_record(ctx.ptr_glb, 1, 3, 0)

    total = 0
    for i in range(n):
        total += proc1(ctx, ctx.ptr_glb)
        total += proc2(ctx, 2)
        total += proc7(2, 3)

        ctx.ptr_glb.enum_comp = proc6(ctx.ptr_glb.enum_comp)
        ctx.ptr_glb.int_comp += 1
        if ctx.ptr_glb.int_comp > 50:
            ctx.ptr_glb.int_comp -= 7

        total += ctx.ptr_glb.int_comp
        total += ctx.ptr_glb.ptr_comp.int_comp
        total += ctx.int_glob

    return total + ctx.ptr_glb.enum_comp + ctx.ptr_glb.int_comp + ctx.ptr_glb.ptr_comp.int_comp
