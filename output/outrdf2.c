/*
 * outrdf2.c	output routines for the Netwide Assembler to produce
 *		RDOFF version 2 format object files, which is used as a
 *		main binary format in the RadiOS (http://radios.sf.net).
 *		Originally Julian planned to use it in his MOSCOW
 *		operating system.
 *
 * The Netwide Assembler is copyright (C) 1996-1998 Simon Tatham and
 * Julian Hall. All rights reserved. The software is
 * redistributable under the licence given in the file "Licence"
 * distributed in the NASM archive.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "nasm.h"
#include "nasmlib.h"
#include "outform.h"

/* VERBOSE_WARNINGS: define this to add some extra warnings... */
#define VERBOSE_WARNINGS

#ifdef OF_RDF2

#include "rdoff/rdoff.h"

/* This signature is written to start of RDOFF files */
static const char *RDOFF2Id = RDOFF2_SIGNATURE;

/* Note that whenever a segment is referred to in the RDOFF file, its number
 * is always half of the segment number that NASM uses to refer to it; this
 * is because NASM only allocates even numbered segments, so as to not
 * waste any of the 16 bits of segment number written to the file - this
 * allows up to 65533 external labels to be defined; otherwise it would be
 * 32764. */

#define COUNT_SEGTYPES 9

static char *segmenttypes[COUNT_SEGTYPES] = {
    "null", "text", "code", "data",
    "comment", "lcomment", "pcomment",
    "symdebug", "linedebug"
};

static int segmenttypenumbers[COUNT_SEGTYPES] = {
    0, 1, 1, 2, 3, 4, 5, 6, 7
};

/* code for managing buffers needed to separate code and data into individual
 * sections until they are ready to be written to the file.
 * We'd better hope that it all fits in memory else we're buggered... */

#define BUF_BLOCK_LEN 4088      /* selected to match page size (4096)
                                 * on 80x86 machines for efficiency */

/***********************************************************************
 * Actual code to deal with RDOFF2 ouput format begins here...
 */

/* global variables set during the initialisation phase */

static struct SAA *seg[RDF_MAXSEGS];    /* seg 0 = code, seg 1 = data */
static struct SAA *header;      /* relocation/import/export records */

static FILE *ofile;

static efunc error;

static struct seginfo {
    char *segname;
    int segnumber;
    uint16 segtype;
    uint16 segreserved;
    long seglength;
} segments[RDF_MAXSEGS];

static int nsegments;

static long bsslength;
static long headerlength;

static void rdf2_init(FILE * fp, efunc errfunc, ldfunc ldef, evalfunc eval)
{
    int segtext, segdata, segbss;

    /* set up the initial segments */
    segments[0].segname = ".text";
    segments[0].segnumber = 0;
    segments[0].segtype = 1;
    segments[0].segreserved = 0;
    segments[0].seglength = 0;

    segments[1].segname = ".data";
    segments[1].segnumber = 1;
    segments[1].segtype = 2;
    segments[1].segreserved = 0;
    segments[1].seglength = 0;

    segments[2].segname = ".bss";
    segments[2].segnumber = 2;
    segments[2].segtype = 0xFFFF;       /* reserved - should never be produced */
    segments[2].segreserved = 0;
    segments[2].seglength = 0;

    nsegments = 3;

    ofile = fp;
    error = errfunc;

    seg[0] = saa_init(1L);
    seg[1] = saa_init(1L);
    seg[2] = NULL;              /* special case! */

    header = saa_init(1L);

    segtext = seg_alloc();
    segdata = seg_alloc();
    segbss = seg_alloc();
    if (segtext != 0 || segdata != 2 || segbss != 4)
        error(ERR_PANIC,
              "rdf segment numbers not allocated as expected (%d,%d,%d)",
              segtext, segdata, segbss);
    bsslength = 0;
    headerlength = 0;
}

static long rdf2_section_names(char *name, int pass, int *bits)
{
    int i;
    char *p, *q;
    int code = -1;
    int reserved = 0;

    /*
     * Default is 32 bits, in the text segment.
     */
    if (!name) {
        *bits = 32;
        return 0;
    }

    /* look for segment type code following segment name */
    p = name;
    while (*p && !isspace(*p))
        p++;
    if (*p) {                   /* we're now in whitespace */
        *p++ = '\0';
        while (*p && isspace(80))
            *p++ = '\0';
    }
    if (*p) {                   /* we're now in an attribute value */
        /*
         * see if we have an optional ',number' following the type code
         */
        if ((q = strchr(p, ','))) {
            *q++ = '\0';

            reserved = readnum(q, &i);
            if (i) {
                error(ERR_NONFATAL,
                      "value following comma must be numeric");
                reserved = 0;
            }
        }
        /*
         * check it against the text strings in segmenttypes 
         */

        for (i = 0; i < COUNT_SEGTYPES; i++)
            if (!nasm_stricmp(p, segmenttypes[i])) {
                code = segmenttypenumbers[i];
                break;
            }
        if (code == -1) {       /* didn't find anything */
            code = readnum(p, &i);
            if (i) {
                error(ERR_NONFATAL, "unrecognised RDF segment type (%s)",
                      p);
                code = 3;
            }
        }
    }
    for (i = 0; i < nsegments; i++) {
        if (!strcmp(name, segments[i].segname)) {
            if (code != -1 || reserved != 0)
                error(ERR_NONFATAL, "segment attributes specified on"
                      " redeclaration of segment");
            return segments[i].segnumber * 2;
        }
    }

    /* declaring a new segment! */

    if (code == -1) {
        error(ERR_NONFATAL, "new segment declared without type code");
        code = 3;
    }
    if (nsegments == RDF_MAXSEGS) {
        error(ERR_FATAL, "reached compiled-in maximum segment limit (%d)",
              RDF_MAXSEGS);
        return NO_SEG;
    }

    segments[nsegments].segname = nasm_strdup(name);
    i = seg_alloc();
    if (i % 2 != 0)
        error(ERR_PANIC, "seg_alloc() returned odd number");
    segments[nsegments].segnumber = i >> 1;
    segments[nsegments].segtype = code;
    segments[nsegments].segreserved = reserved;
    segments[nsegments].seglength = 0;

    seg[nsegments] = saa_init(1L);

    return i;
}

/*
 * Write relocation record
 */
static void write_reloc_rec(struct RelocRec *r)
{
    char buf[4], *b;

    if (r->refseg != (uint16) NO_SEG && (r->refseg & 1))        /* segment base ref */
        r->type = RDFREC_SEGRELOC;

    r->refseg >>= 1;            /* adjust segment nos to RDF rather than NASM */

    saa_wbytes(header, &r->type, 1);
    saa_wbytes(header, &r->reclen, 1);
    saa_wbytes(header, &r->segment, 1);
    b = buf;
    WRITELONG(b, r->offset);
    saa_wbytes(header, buf, 4);
    saa_wbytes(header, &r->length, 1);
    b = buf;
    WRITESHORT(b, r->refseg);
    saa_wbytes(header, buf, 2);
    headerlength += r->reclen + 2;
}

/*
 * Write export record
 */
static void write_export_rec(struct ExportRec *r)
{
    char buf[4], *b;

    r->segment >>= 1;

    saa_wbytes(header, &r->type, 1);
    saa_wbytes(header, &r->reclen, 1);
    saa_wbytes(header, &r->flags, 1);
    saa_wbytes(header, &r->segment, 1);
    b = buf;
    WRITELONG(b, r->offset);
    saa_wbytes(header, buf, 4);
    saa_wbytes(header, r->label, strlen(r->label) + 1);
    headerlength += r->reclen + 2;
}

static void write_import_rec(struct ImportRec *r)
{
    char buf[4], *b;

    r->segment >>= 1;

    saa_wbytes(header, &r->type, 1);
    saa_wbytes(header, &r->reclen, 1);
    saa_wbytes(header, &r->flags, 1);
    b = buf;
    WRITESHORT(b, r->segment);
    saa_wbytes(header, buf, 2);
    saa_wbytes(header, r->label, strlen(r->label) + 1);
    headerlength += r->reclen + 2;
}

/*
 * Write BSS record
 */
static void write_bss_rec(struct BSSRec *r)
{
    char buf[4], *b;

    saa_wbytes(header, &r->type, 1);
    saa_wbytes(header, &r->reclen, 1);
    b = buf;
    WRITELONG(b, r->amount);
    saa_wbytes(header, buf, 4);
    headerlength += r->reclen + 2;
}

/*
 * Write common variable record
 */
static void write_common_rec(struct CommonRec *r)
{
    char buf[4], *b;

    r->segment >>= 1;

    saa_wbytes(header, &r->type, 1);
    saa_wbytes(header, &r->reclen, 1);
    b = buf;
    WRITESHORT(b, r->segment);
    saa_wbytes(header, buf, 2);
    b = buf;
    WRITELONG(b, r->size);
    saa_wbytes(header, buf, 4);
    b = buf;
    WRITESHORT(b, r->align);
    saa_wbytes(header, buf, 2);
    saa_wbytes(header, r->label, strlen(r->label) + 1);
    headerlength += r->reclen + 2;
}

/*
 * Write library record
 */
static void write_dll_rec(struct DLLRec *r)
{
    saa_wbytes(header, &r->type, 1);
    saa_wbytes(header, &r->reclen, 1);
    saa_wbytes(header, r->libname, strlen(r->libname) + 1);
    headerlength += r->reclen + 2;
}

/*
 * Write module name record
 */
static void write_modname_rec(struct ModRec *r)
{
    saa_wbytes(header, &r->type, 1);
    saa_wbytes(header, &r->reclen, 1);
    saa_wbytes(header, r->modname, strlen(r->modname) + 1);
    headerlength += r->reclen + 2;
}

/*
 * Handle export, import and common records.
 */
static void rdf2_deflabel(char *name, long segment, long offset,
                          int is_global, char *special)
{
    struct ExportRec r;
    struct ImportRec ri;
    struct CommonRec ci;
    static int farsym = 0;
    static int i;
    byte symflags = 0;
    int len;

    /* Check if the label length is OK */
    if ((len = strlen(name)) >= EXIM_LABEL_MAX) {
        error(ERR_NONFATAL, "label size exceeds %d bytes", EXIM_LABEL_MAX);
        return;
    }
    if (!len) {
        error(ERR_NONFATAL, "zero-length label");
        return;
    }

    if (is_global == 2) {
        /* Common variable */
        ci.type = RDFREC_COMMON;
        ci.size = offset;
        ci.segment = segment;
        strcpy(ci.label, name);
        ci.reclen = 9 + len;
        ci.align = 0;

        /*
         * Check the special text to see if it's a valid number and power
         * of two; if so, store it as the alignment for the common variable.
         */
        if (special) {
            int err;
            ci.align = readnum(special, &err);
            if (err)
                error(ERR_NONFATAL, "alignment constraint `%s' is not a"
                      " valid number", special);
            else if ((ci.align | (ci.align - 1)) != 2 * ci.align - 1)
                error(ERR_NONFATAL, "alignment constraint `%s' is not a"
                      " power of two", special);
        }
        write_common_rec(&ci);
    }

    /* We don't care about local labels or fix-up hints */
    if (is_global != 1)
        return;

    if (special) {
        while (*special == ' ' || *special == '\t')
            special++;

        if (!nasm_strnicmp(special, "export", 6)) {
            special += 6;
            symflags |= SYM_GLOBAL;
        } else if (!nasm_strnicmp(special, "import", 6)) {
            special += 6;
            symflags |= SYM_IMPORT;
        }

        if (*special) {
            while (isspace(*special))
                special++;
            if (!nasm_stricmp(special, "far")) {
                farsym = 1;
            } else if (!nasm_stricmp(special, "near")) {
                farsym = 0;
            } else if (!nasm_stricmp(special, "proc") ||
                       !nasm_stricmp(special, "function")) {
                symflags |= SYM_FUNCTION;
            } else if (!nasm_stricmp(special, "data") ||
                       !nasm_stricmp(special, "object")) {
                symflags |= SYM_DATA;
            } else
                error(ERR_NONFATAL, "unrecognised symbol type `%s'",
                      special);
        }
    }

    if (name[0] == '.' && name[1] == '.' && name[2] != '@') {
        error(ERR_NONFATAL, "unrecognised special symbol `%s'", name);
        return;
    }

    for (i = 0; i < nsegments; i++) {
        if (segments[i].segnumber == segment >> 1)
            break;
    }

    if (i >= nsegments) {       /* EXTERN declaration */
        ri.type = farsym ? RDFREC_FARIMPORT : RDFREC_IMPORT;
        if (symflags & SYM_GLOBAL)
            error(ERR_NONFATAL,
                  "symbol type conflict - EXTERN cannot be EXPORT");
        ri.flags = symflags;
        ri.segment = segment;
        strcpy(ri.label, name);
        ri.reclen = 4 + len;
        write_import_rec(&ri);
    } else if (is_global) {
        r.type = RDFREC_GLOBAL; /* GLOBAL declaration */
        if (symflags & SYM_IMPORT)
            error(ERR_NONFATAL,
                  "symbol type conflict - GLOBAL cannot be IMPORT");
        r.flags = symflags;
        r.segment = segment;
        r.offset = offset;
        strcpy(r.label, name);
        r.reclen = 7 + len;
        write_export_rec(&r);
    }
}

static void membufwrite(int segment, const void *data, int bytes)
{
    int i;
    char buf[4], *b;

    for (i = 0; i < nsegments; i++) {
        if (segments[i].segnumber == segment)
            break;
    }
    if (i == nsegments)
        error(ERR_PANIC, "can't find segment %d", segment);

    if (bytes < 0) {
        b = buf;
        if (bytes == -2)
            WRITESHORT(b, *(short *)data);
        else
            WRITELONG(b, *(long *)data);
        data = buf;
        bytes = -bytes;
    }
    segments[i].seglength += bytes;
    saa_wbytes(seg[i], data, bytes);
}

static int getsegmentlength(int segment)
{
    int i;
    for (i = 0; i < nsegments; i++) {
        if (segments[i].segnumber == segment)
            break;
    }
    if (i == nsegments)
        error(ERR_PANIC, "can't find segment %d", segment);

    return segments[i].seglength;
}

static void rdf2_out(long segto, const void *data, unsigned long type,
                     long segment, long wrt)
{
    long bytes = type & OUT_SIZMASK;
    struct RelocRec rr;
    unsigned char databuf[4], *pd;
    int seg;

    if (segto == NO_SEG) {
        if ((type & OUT_TYPMASK) != OUT_RESERVE)
            error(ERR_NONFATAL,
                  "attempt to assemble code in ABSOLUTE space");
        return;
    }

    segto >>= 1;                /* convert NASM segment no to RDF number */

    for (seg = 0; seg < nsegments; seg++) {
        if (segments[seg].segnumber == segto)
            break;
    }
    if (seg >= nsegments) {
        error(ERR_NONFATAL,
              "specified segment not supported by rdf output format");
        return;
    }

    if (wrt != NO_SEG) {
        wrt = NO_SEG;           /* continue to do _something_ */
        error(ERR_NONFATAL, "WRT not supported by rdf output format");
    }

    type &= OUT_TYPMASK;

    if (segto == 2 && type != OUT_RESERVE) {
        error(ERR_NONFATAL, "BSS segments may not be initialised");

        /* just reserve the space for now... */

        if (type == OUT_REL2ADR)
            bytes = 2;
        else
            bytes = 4;
        type = OUT_RESERVE;
    }

    if (type == OUT_RESERVE) {
        if (segto == 2)         /* BSS segment space reserverd */
            bsslength += bytes;
        else
            while (bytes--)
                membufwrite(segto, databuf, 1);
    } else if (type == OUT_RAWDATA) {
        if (segment != NO_SEG)
            error(ERR_PANIC, "OUT_RAWDATA with other than NO_SEG");

        membufwrite(segto, data, bytes);
    } else if (type == OUT_ADDRESS) {

        /* if segment == NO_SEG then we are writing an address of an
           object within the same segment - do not produce reloc rec. */

        /* FIXME - is this behaviour sane? at first glance it doesn't
           appear to be. Must test this thoroughly...! */

        if (segment != NO_SEG) {
            /* it's an address, so we must write a relocation record */

            rr.type = RDFREC_RELOC;     /* type signature */
            rr.reclen = 8;
            rr.segment = segto; /* segment we're currently in */
            rr.offset = getsegmentlength(segto);        /* current offset */
            rr.length = bytes;  /* length of reference */
            rr.refseg = segment;        /* segment referred to */
            write_reloc_rec(&rr);
        }

        pd = databuf;           /* convert address to little-endian */
        if (bytes == 2)
            WRITESHORT(pd, *(long *)data);
        else
            WRITELONG(pd, *(long *)data);

        membufwrite(segto, databuf, bytes);

    } else if (type == OUT_REL2ADR) {
        if (segment == segto)
            error(ERR_PANIC, "intra-segment OUT_REL2ADR");

        rr.reclen = 8;
        rr.offset = getsegmentlength(segto);    /* current offset */
        rr.length = 2;          /* length of reference */
        rr.refseg = segment;    /* segment referred to (will be >>1'd) */

        if (segment != NO_SEG && segment % 2) {
            rr.type = RDFREC_SEGRELOC;
            rr.segment = segto; /* memory base refs *aren't ever* relative! */
            write_reloc_rec(&rr);

            /* what do we put in the code? Simply the data. This should almost
             * always be zero, unless someone's doing segment arithmetic...
             */
            rr.offset = *(long *)data;
        } else {
            rr.type = RDFREC_RELOC;     /* type signature */
            rr.segment = segto + 64;    /* segment we're currently in + rel flag */
            write_reloc_rec(&rr);

            /* work out what to put in the code: offset of the end of this operand,
             * subtracted from any data specified, so that loader can just add
             * address of imported symbol onto it to get address relative to end of
             * instruction: import_address + data(offset) - end_of_instrn */

            rr.offset = *(long *)data - (rr.offset + bytes);
        }

        membufwrite(segto, &rr.offset, -2);
    } else if (type == OUT_REL4ADR) {
        if (segment == segto)
            error(ERR_PANIC, "intra-segment OUT_REL4ADR");
        if (segment != NO_SEG && segment % 2) {
            error(ERR_PANIC, "erm... 4 byte segment base ref?");
        }

        rr.type = RDFREC_RELOC; /* type signature */
        rr.segment = segto + 64;        /* segment we're currently in + rel tag */
        rr.offset = getsegmentlength(segto);    /* current offset */
        rr.length = 4;          /* length of reference */
        rr.refseg = segment;    /* segment referred to */
        rr.reclen = 8;
        write_reloc_rec(&rr);

        rr.offset = *(long *)data - (rr.offset + bytes);

        membufwrite(segto, &rr.offset, -4);
    }
}

static void rdf2_cleanup(int debuginfo)
{
    long l;
    struct BSSRec bs;
    int i;

    (void)debuginfo;

    /* should write imported & exported symbol declarations to header here */

    /* generate the output file... */
    fwrite(RDOFF2Id, 6, 1, ofile);      /* file type magic number */

    if (bsslength != 0) {       /* reserve BSS */
        bs.type = RDFREC_BSS;
        bs.amount = bsslength;
        bs.reclen = 4;
        write_bss_rec(&bs);
    }

    /*
     * calculate overall length of the output object
     */
    l = headerlength + 4;

    for (i = 0; i < nsegments; i++) {
        if (i == 2)
            continue;           /* skip BSS segment */
        l += 10 + segments[i].seglength;
    }
    l += 10;                    /* null segment */

    fwritelong(l, ofile);

    fwritelong(headerlength, ofile);
    saa_fpwrite(header, ofile); /* dump header */
    saa_free(header);

    for (i = 0; i < nsegments; i++) {
        if (i == 2)
            continue;

        fwriteshort(segments[i].segtype, ofile);
        fwriteshort(segments[i].segnumber, ofile);
        fwriteshort(segments[i].segreserved, ofile);
        fwritelong(segments[i].seglength, ofile);

        saa_fpwrite(seg[i], ofile);
        saa_free(seg[i]);
    }

    /* null segment - write 10 bytes of zero */
    fwritelong(0, ofile);
    fwritelong(0, ofile);
    fwriteshort(0, ofile);

    fclose(ofile);
}

static long rdf2_segbase(long segment)
{
    return segment;
}

/*
 * Handle RDOFF2 specific directives
 */
static int rdf2_directive(char *directive, char *value, int pass)
{
    int n;

    /* Check if the name length is OK */
    if ((n = strlen(value)) >= MODLIB_NAME_MAX) {
        error(ERR_NONFATAL, "name size exceeds %d bytes", MODLIB_NAME_MAX);
        return 0;
    }

    if (!strcmp(directive, "library")) {
        if (pass == 1) {
            struct DLLRec r;
            r.type = RDFREC_DLL;
            r.reclen = n + 1;
            strcpy(r.libname, value);
            write_dll_rec(&r);
        }
        return 1;
    }

    if (!strcmp(directive, "module")) {
        if (pass == 1) {
            struct ModRec r;
            r.type = RDFREC_MODNAME;
            r.reclen = n + 1;
            strcpy(r.modname, value);
            write_modname_rec(&r);
        }
        return 1;
    }

    return 0;
}

static void rdf2_filename(char *inname, char *outname, efunc error)
{
    standard_extension(inname, outname, ".rdf", error);
}

static const char *rdf2_stdmac[] = {
    "%define __SECT__ [section .text]",
    "%imacro library 1+.nolist",
    "[library %1]",
    "%endmacro",
    "%imacro module 1+.nolist",
    "[module %1]",
    "%endmacro",
    "%macro __NASM_CDecl__ 1",
    "%endmacro",
    NULL
};

static int rdf2_set_info(enum geninfo type, char **val)
{
    return 0;
}

struct ofmt of_rdf2 = {
    "Relocatable Dynamic Object File Format v2.0",
    "rdf",
    NULL,
    null_debug_arr,
    &null_debug_form,
    rdf2_stdmac,
    rdf2_init,
    rdf2_set_info,
    rdf2_out,
    rdf2_deflabel,
    rdf2_section_names,
    rdf2_segbase,
    rdf2_directive,
    rdf2_filename,
    rdf2_cleanup
};

#endif                          /* OF_RDF2 */
