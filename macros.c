/* This file auto-generated from standard.mac by macros.pl - don't edit it */

#include <stddef.h>

static const char *stdmac[] = {
    "%idefine IDEAL",
    "%idefine JUMPS",
    "%idefine P386",
    "%idefine P486",
    "%idefine P586",
    "%idefine END",
    "%define __FILE__",
    "%define __LINE__",
    "%define __SECT__",
    "%imacro section 1+.nolist",
    "%define __SECT__ [section %1]",
    "__SECT__",
    "%endmacro",
    "%imacro segment 1+.nolist",
    "%define __SECT__ [segment %1]",
    "__SECT__",
    "%endmacro",
    "%imacro absolute 1+.nolist",
    "%define __SECT__ [absolute %1]",
    "__SECT__",
    "%endmacro",
    "%imacro struc 1.nolist",
    "%push struc",
    "%define %$strucname %1",
    "[absolute 0]",
    "%$strucname:",
    "%endmacro",
    "%imacro endstruc 0.nolist",
    "%{$strucname}_size:",
    "%pop",
    "__SECT__",
    "%endmacro",
    "%imacro istruc 1.nolist",
    "%push istruc",
    "%define %$strucname %1",
    "%$strucstart:",
    "%endmacro",
    "%imacro at 1-2+.nolist",
    "times %1-($-%$strucstart) db 0",
    "%2",
    "%endmacro",
    "%imacro iend 0.nolist",
    "times %{$strucname}_size-($-%$strucstart) db 0",
    "%pop",
    "%endmacro",
    "%imacro align 1-2+.nolist nop",
    "times ($$-$) & ((%1)-1) %2",
    "%endmacro",
    "%imacro alignb 1-2+.nolist resb 1",
    "times ($$-$) & ((%1)-1) %2",
    "%endmacro",
    "%imacro extern 1-*.nolist",
    "%rep %0",
    "[extern %1]",
    "%rotate 1",
    "%endrep",
    "%endmacro",
    "%imacro bits 1+.nolist",
    "[bits %1]",
    "%endmacro",
    "%imacro use16 0.nolist",
    "[bits 16]",
    "%endmacro",
    "%imacro use32 0.nolist",
    "[bits 32]",
    "%endmacro",
    "%imacro global 1-*.nolist",
    "%rep %0",
    "[global %1]",
    "%rotate 1",
    "%endrep",
    "%endmacro",
    "%imacro common 1-*.nolist",
    "%rep %0",
    "[common %1]",
    "%rotate 1",
    "%endrep",
    "%endmacro",
    "%imacro cpu 1+.nolist",
    "[cpu %1]",
    "%endmacro",
    "%define __NASM_MAJOR__ 0",
    "%define __NASM_MINOR__ 98",
    "%define __NASM_SUBMINOR__ 39",
    "%define __NASM_PATCHLEVEL__ 0",
    "%define __NASM_VERSION_ID__ 000622700h",
    "%define __NASM_VER__ \"0.98.39\"",
    NULL
};
#define TASM_MACRO_COUNT 6
