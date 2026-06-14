# Adapted from the Computer Language Benchmarks Game pystone.python3.
# Source: https://github.com/dundee/pybenchmarks/blob/master/bencher/programs/pystone/pystone.python3
#
# The Computer Language Benchmarks Game
# Copyright 2008-2012 Isaac Gouy
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
#    Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
#    Neither the name of "The Computer Language Benchmarks Game" nor the name of
#    "The Computer Language Shootout Benchmarks" nor the name "bencher" nor the
#    names of its contributors may be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# CloverVM adaptation notes:
# - the benchmark harness externally times run(n), so pystone's internal timing
#   and null-loop calibration are removed;
# - setup-only unsupported constructs are rewritten: list multiplication, list
#   comprehensions, and unpacking assignment;
# - Proc0 returns a deterministic integer checksum for harness validation.

Ident1 = 1
Ident2 = 2
Ident3 = 3
Ident4 = 4
Ident5 = 5

TRUE = 1
FALSE = 0


class Record:
    def __init__(
        self, PtrComp=None, Discr=0, EnumComp=0, IntComp=0, StringComp=0
    ):
        self.PtrComp = PtrComp
        self.Discr = Discr
        self.EnumComp = EnumComp
        self.IntComp = IntComp
        self.StringComp = StringComp

    def copy(self):
        return Record(
            self.PtrComp,
            self.Discr,
            self.EnumComp,
            self.IntComp,
            self.StringComp,
        )


def make_zero_array():
    result = []
    for i in range(51):
        result.append(0)
    return result


def make_zero_matrix():
    result = []
    for i in range(51):
        result.append(make_zero_array())
    return result


IntGlob = 0
BoolGlob = FALSE
Char1Glob = "\0"
Char2Glob = "\0"
Array1Glob = None
Array2Glob = None
PtrGlb = None
PtrGlbNext = None


def run(n):
    return Proc0(n)


def Proc0(loops):
    global IntGlob
    global BoolGlob
    global Char1Glob
    global Char2Glob
    global Array1Glob
    global Array2Glob
    global PtrGlb
    global PtrGlbNext

    IntGlob = 0
    BoolGlob = FALSE
    Char1Glob = "\0"
    Char2Glob = "\0"
    Array1Glob = make_zero_array()
    Array2Glob = make_zero_matrix()

    PtrGlbNext = Record()
    PtrGlb = Record()
    PtrGlb.PtrComp = PtrGlbNext
    PtrGlb.Discr = Ident1
    PtrGlb.EnumComp = Ident3
    PtrGlb.IntComp = 40
    PtrGlb.StringComp = "DHRYSTONE PROGRAM, SOME STRING"
    String1Loc = "DHRYSTONE PROGRAM, 1'ST STRING"
    Array2Glob[8][7] = 10

    for i in range(loops):
        Proc5()
        Proc4()
        IntLoc1 = 2
        IntLoc2 = 3
        String2Loc = "DHRYSTONE PROGRAM, 2'ND STRING"
        EnumLoc = Ident2
        BoolGlob = not Func2(String1Loc, String2Loc)
        while IntLoc1 < IntLoc2:
            IntLoc3 = 5 * IntLoc1 - IntLoc2
            IntLoc3 = Proc7(IntLoc1, IntLoc2)
            IntLoc1 = IntLoc1 + 1
        Proc8(Array1Glob, Array2Glob, IntLoc1, IntLoc3)
        PtrGlb = Proc1(PtrGlb)
        CharIndex = "A"
        while CharIndex <= Char2Glob:
            if EnumLoc == Func1(CharIndex, "C"):
                EnumLoc = Proc6(Ident1)
            CharIndex = chr(ord(CharIndex) + 1)
        IntLoc3 = IntLoc2 * IntLoc1
        IntLoc2 = IntLoc3 / IntLoc1
        IntLoc2 = 7 * (IntLoc3 - IntLoc2) - IntLoc1
        IntLoc1 = Proc2(IntLoc1)

    result = IntGlob
    if BoolGlob:
        result = result + 1
    if Char1Glob == "A":
        result = result + 10
    if Char2Glob == "B":
        result = result + 100
    result = result + Array1Glob[8]
    result = result + Array1Glob[9]
    result = result + Array1Glob[38]
    result = result + Array2Glob[8][7]
    result = result + Array2Glob[8][8]
    result = result + Array2Glob[28][8]
    result = result + PtrGlb.Discr
    result = result + PtrGlb.EnumComp
    result = result + PtrGlb.IntComp
    result = result + PtrGlb.PtrComp.IntComp
    PtrGlb = None
    PtrGlbNext = None
    Array1Glob = None
    Array2Glob = None
    return result


def Proc1(PtrParIn):
    NextRecord = PtrGlb.copy()
    PtrParIn.PtrComp = NextRecord
    PtrParIn.IntComp = 5
    NextRecord.IntComp = PtrParIn.IntComp
    NextRecord.PtrComp = PtrParIn.PtrComp
    NextRecord.PtrComp = Proc3(NextRecord.PtrComp)
    if NextRecord.Discr == Ident1:
        NextRecord.IntComp = 6
        NextRecord.EnumComp = Proc6(PtrParIn.EnumComp)
        NextRecord.PtrComp = PtrGlb.PtrComp
        NextRecord.IntComp = Proc7(NextRecord.IntComp, 10)
    else:
        PtrParIn = NextRecord.copy()
    NextRecord.PtrComp = None
    return PtrParIn


def Proc2(IntParIO):
    IntLoc = IntParIO + 10
    while 1:
        if Char1Glob == "A":
            IntLoc = IntLoc - 1
            IntParIO = IntLoc - IntGlob
            EnumLoc = Ident1
        if EnumLoc == Ident1:
            break
    return IntParIO


def Proc3(PtrParOut):
    global IntGlob

    if PtrGlb is not None:
        PtrParOut = PtrGlb.PtrComp
    else:
        IntGlob = 100
    PtrGlb.IntComp = Proc7(10, IntGlob)
    return PtrParOut


def Proc4():
    global Char2Glob

    BoolLoc = Char1Glob == "A"
    BoolLoc = BoolLoc or BoolGlob
    Char2Glob = "B"


def Proc5():
    global Char1Glob
    global BoolGlob

    Char1Glob = "A"
    BoolGlob = FALSE


def Proc6(EnumParIn):
    EnumParOut = EnumParIn
    if not Func3(EnumParIn):
        EnumParOut = Ident4
    if EnumParIn == Ident1:
        EnumParOut = Ident1
    elif EnumParIn == Ident2:
        if IntGlob > 100:
            EnumParOut = Ident1
        else:
            EnumParOut = Ident4
    elif EnumParIn == Ident3:
        EnumParOut = Ident2
    elif EnumParIn == Ident4:
        pass
    elif EnumParIn == Ident5:
        EnumParOut = Ident3
    return EnumParOut


def Proc7(IntParI1, IntParI2):
    IntLoc = IntParI1 + 2
    IntParOut = IntParI2 + IntLoc
    return IntParOut


def Proc8(Array1Par, Array2Par, IntParI1, IntParI2):
    global IntGlob

    IntLoc = IntParI1 + 5
    Array1Par[IntLoc] = IntParI2
    Array1Par[IntLoc + 1] = Array1Par[IntLoc]
    Array1Par[IntLoc + 30] = IntLoc
    for IntIndex in range(IntLoc, IntLoc + 2):
        Array2Par[IntLoc][IntIndex] = IntLoc
    Array2Par[IntLoc][IntLoc - 1] = Array2Par[IntLoc][IntLoc - 1] + 1
    Array2Par[IntLoc + 20][IntLoc] = Array1Par[IntLoc]
    IntGlob = 5


def Func1(CharPar1, CharPar2):
    CharLoc1 = CharPar1
    CharLoc2 = CharLoc1
    if CharLoc2 != CharPar2:
        return Ident1
    else:
        return Ident2


def Func2(StrParI1, StrParI2):
    IntLoc = 1
    while IntLoc <= 1:
        if Func1(StrParI1[IntLoc], StrParI2[IntLoc + 1]) == Ident1:
            CharLoc = "A"
            IntLoc = IntLoc + 1
    if CharLoc >= "W" and CharLoc <= "Z":
        IntLoc = 7
    if CharLoc == "X":
        return TRUE
    else:
        if StrParI1 > StrParI2:
            IntLoc = IntLoc + 7
            return TRUE
        else:
            return FALSE


def Func3(EnumParIn):
    EnumLoc = EnumParIn
    if EnumLoc == Ident3:
        return TRUE
    return FALSE
