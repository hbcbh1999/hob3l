/* -*- Mode: C -*- */
/* Copyright (C) 2018 by Henrik Theiling, License: GPLv3, see LICENSE file */

#include <stdio.h>
#include <hob3lbase/mat.h>
#include <hob3lbase/pool.h>
#include <hob3lbase/alloc.h>
#include <hob3l/syn.h>
#include <hob3l/scad.h>
#include <hob3l/csg3.h>
#include <hob3l/csg2.h>
#include <hob3l/ps.h>
#include "internal.h"

#ifndef CP_PROG_NAME
#define CP_PROG_NAME "hob3l"
#endif

typedef struct {
    cp_dim_t z_min;
    cp_dim_t z_max;
    cp_dim_t z_step;
    bool have_z_min;
    bool have_z_max;
    bool dump_syn;
    bool dump_scad;
    bool dump_csg3;
    bool dump_csg2;
    bool dump_ps;
    bool dump_stl;
    bool dump_js;
    bool have_dump;
    bool no_tri;
    bool no_csg;
    bool no_diff;
    unsigned verbose;
    unsigned ps_scale_step; /* 0 = no change, 1 = normal bb, 2 = max bb */
    cp_ps_opt_t ps;
    cp_scale_t ps_persp;
    char const *out_file_name;
    cp_csg_opt_t csg;
} cp_opt_t;

static bool next_i(
    size_t *ip,
    size_t *i_alloc,
    size_t i_count)
{
    size_t i = (*i_alloc)++;
    if (i < i_count) {
        *ip = i;
        return true;
    }
    return false;
}

/**
 * Process for each layer the CSG and then its triangulation
 *
 * This can theoretically be run in multiple threads: each thread
 * needs its own pool, and next_i needs to be made atomic.
 */
static bool process_stack_csg(
    cp_opt_t *opt,
    cp_pool_t *pool,
    cp_err_t *err,
    cp_csg2_tree_t *csg2,
    cp_csg2_tree_t *csg2b,
    cp_csg2_tree_t *csg2_out,
    size_t *zi_p,
    size_t  zi_count)
{
    size_t i;
    while (next_i(&i, zi_p, zi_count)) {
        cp_pool_clear(pool);
        if (!cp_csg2_tree_add_layer(pool, csg2, err, i)) {
            return false;
        }
        if (!opt->no_csg) {
            cp_csg2_op_add_layer(&opt->csg, pool, csg2b, csg2, i);
        }
        if (!opt->no_tri) {
            if (!cp_csg2_tri_layer(pool, err, csg2_out, i)) {
                return false;
            }
        }
    }
    return true;
}

/**
 * Second run through the layer stack: XOR between two layers plus its triangulation
 *
 * This can theoretically be run in multiple threads: each thread
 * needs its own pool, and next_i needs to be made atomic.
 */
static bool process_stack_diff(
    cp_opt_t *opt,
    cp_pool_t *pool,
    cp_err_t *err,
    cp_csg2_tree_t *csg2_out,
    size_t *zi_p,
    size_t  zi_count)
{
    size_t i;
    while (next_i(&i, zi_p, zi_count)) {
        cp_pool_clear(pool);
        cp_csg2_op_diff_layer(&opt->csg, pool, csg2_out, i);
        if (!opt->no_tri) {
            if (!cp_csg2_tri_layer_diff(pool, err, csg2_out, i)) {
                return false;
            }
        }
    }
    return true;
}

static bool do_file(
    cp_stream_t *sout,
    cp_opt_t *opt,
    cp_syn_tree_t *r,
    const char *fn,
    FILE *f)
{
    /* stage 1: syntax tree */
    if (!cp_syn_parse(r, fn, f)) {
        return false;
    }
    if (opt->dump_syn) {
        cp_syn_tree_put_scad(sout, r);
        return true;
    }

    /* stage 2: SCAD */
    cp_scad_tree_t *scad = CP_NEW(*scad);
    if (!cp_scad_from_syn_tree(scad, r)) {
        return false;
    }
    if (opt->dump_scad) {
        cp_scad_tree_put_scad(sout, scad);
        return true;
    }

    /* pool for tmp objects */
    cp_pool_t pool;
    cp_pool_init(&pool, 0);

    /* stage 3: 3D CSG */
    cp_csg3_tree_t *csg3 = CP_NEW(*csg3);
    csg3->opt = &opt->csg;
    if (!cp_csg3_from_scad_tree(&pool, r, csg3, &r->err, scad)) {
        return false;
    }

    cp_vec3_minmax_t full_bb = CP_VEC3_MINMAX_EMPTY;
    if (csg3->root != NULL) {
        cp_csg3_tree_bb(&full_bb, csg3, true);
#ifdef PSTRACE
        cp_ps_xform_from_bb(&cp_debug_ps_xform,
            full_bb.min.x, full_bb.min.y,
            full_bb.max.x, full_bb.max.y);
        cp_debug_ps_xform.add_x -= CP_PS_PAPER_X/2;
        cp_debug_ps_xform.add_y -= CP_PS_PAPER_Y/2;
        cp_debug_ps_xform.add_x *= cp_debug_ps_scale_x;
        cp_debug_ps_xform.add_y *= cp_debug_ps_scale_y;
        cp_debug_ps_xform.mul_x *= cp_debug_ps_scale_x;
        cp_debug_ps_xform.mul_y *= cp_debug_ps_scale_y;
        cp_debug_ps_xform.add_x += CP_PS_PAPER_X/2;
        cp_debug_ps_xform.add_y += CP_PS_PAPER_Y/2;
        cp_debug_ps_xform.add_x += (cp_debug_ps_xlat_x * cp_debug_ps_xform.mul_x);
        cp_debug_ps_xform.add_y += (cp_debug_ps_xlat_y * cp_debug_ps_xform.mul_y);
#endif
    }

    if (opt->dump_csg3) {
        cp_csg3_tree_put_scad(sout, csg3);
        return true;
    }

    /* Get bounding box (normal one, i.e., ignoring stuff that is subtracted) */
    cp_vec3_minmax_t bb = CP_VEC3_MINMAX_EMPTY;
    cp_csg3_tree_bb(&bb, csg3, false);

    /* stage 4: 2D CSG */
    cp_dim_t z_min = bb.min.z + opt->z_step/2;
    cp_dim_t z_max = bb.max.z;
    if (opt->have_z_min) {
        z_min = opt->z_min;
    }
    if (opt->have_z_max) {
        z_max = opt->z_max;
    }

    cp_range_t range;
    cp_range_init(&range, z_min, z_max, opt->z_step);
    if (range.cnt == 0) {
        range.cnt = 1;
    }

    if (opt->verbose >= 1) {
        fprintf(stderr, "Info: Z: min=%g, step=%g, layer_cnt=%"_Pz"u, max=%g\n",
            range.min, range.step, range.cnt,
            range.min + (range.step * cp_f(range.cnt - 1)));
    }

    /* process layer by layer: extract layer, slice, triangulate */
    cp_csg2_tree_t *csg2 = CP_NEW(*csg2);
    cp_csg2_tree_from_csg3(csg2, csg3, &range, &opt->csg);

    cp_csg2_tree_t *csg2b = CP_NEW(*csg2b);
    cp_csg2_op_tree_init(csg2b, csg2);

    cp_csg2_tree_t *csg2_out = opt->no_csg ? csg2 : csg2b;
    size_t zi = 0;
    if (!process_stack_csg(opt, &pool, &r->err, csg2, csg2b, csg2_out, &zi, range.cnt)) {
        return false;
    }

    /* compute diff if there is any output format that can use it */
    if (opt->dump_js) {
        if (!opt->no_diff) {
            zi = 0;
            if (!process_stack_diff(opt, &pool, &r->err, csg2_out, &zi, range.cnt)) {
                return false;
            }
        }
    }

    /* print */
    if (opt->dump_csg2) {
        cp_csg2_tree_put_scad(sout, csg2_out);
        return true;
    }
    if (opt->dump_stl) {
        cp_csg2_tree_put_stl(sout, csg2_out);
        return true;
    }
    if (opt->dump_js) {
        cp_csg2_tree_put_js(sout, csg2_out);
        return true;
    }
    if (opt->dump_ps) {
        cp_ps_xform_t xform = CP_PS_XFORM_MM;
        switch (opt->ps_scale_step) {
        default:
            break;

        case 1:
            cp_ps_xform_from_bb(&xform,
                bb.min.x, bb.min.y,
                bb.max.x, bb.max.y);
            break;

        case 2:
            cp_ps_xform_from_bb(&xform,
                full_bb.min.x, full_bb.min.y,
                full_bb.max.x, full_bb.max.y);
            break;
        }
        opt->ps.xform1 = &xform;
        cp_csg2_tree_put_ps(sout, &opt->ps, csg2_out);
        return true;
    }

    return true;
}

__attribute__((noreturn))
static void my_exit(int i)
{
#ifdef PSTRACE
    if (cp_debug_ps != NULL) {
        cp_ps_doc_end(cp_debug_ps, cp_debug_ps_page_cnt, 0, 0, -1, -1);
        CP_FREE(cp_debug_ps);
    }
    if (cp_debug_ps_file != NULL) {
        fclose(cp_debug_ps_file);
        cp_debug_ps_file = NULL;
    }
#endif
    exit(i);
}

static char const *cp_prog_name(void)
{
    return CP_PROG_NAME;
}

static char const *opt_help;

__attribute__((noreturn))
static void help(void)
{
#define PRI printf
    PRI("Usage: %s [Options] INFILE\n", cp_prog_name());
    PRI("\n");
    PRI("This reads 3D CSG models from (simple syntax) SCAD files, slices\n"
        "them into layers of 2D CSG models, applies 2D CSG boolean operations\n"
        "to the resulting polygon stack (instead of the 3D polyhedra), and outputs the\n"
        "result as STL file consisting of a (trivially extruded) polygon per slice.\n");
    PRI("\n");
    PRI("Options:\n");
    PRI("%s", opt_help);
#undef PRI
    my_exit(0);
}

static void get_arg_bool(
    bool *v,
    char const *arg,
    char const *str)
{
    if ((str == NULL) ||
        strequ(str, "true") ||
        strequ(str, "1") ||
        strequ(str, "yes"))
    {
        *v = true;
        return;
    }
    if (strequ(str, "false") ||
        strequ(str, "0") ||
        strequ(str, "no"))
    {
        *v = false;
        return;
    }
    fprintf(stderr, "Error: %s: invalid boolean: '%s'\n", arg, str);
    my_exit(1);
}

static void get_arg_err(
    unsigned *v,
    char const *arg,
    char const *str)
{
    if (strequ(str, "fail") ||
        strequ(str, "error") ||
        strequ(str, "err"))
    {
        *v = CP_ERR_FAIL;
        return;
    }

    if (strequ(str, "ign") ||
        strequ(str, "ignore"))
    {
        *v = CP_ERR_IGNORE;
        return;
    }

    if (strequ(str, "warn") ||
        strequ(str, "warning"))
    {
        *v = CP_ERR_WARN;
        return;
    }

    fprintf(stderr, "Error: %s: invalid problem handling: '%s', expected 'error' or 'ignore'\n",
        arg, str);
    my_exit(1);
}

static void get_arg_neg_bool(
    bool *v,
    char const *arg,
    char const *str)
{
    bool x = false;
    get_arg_bool(&x, arg, str);
    *v = !x;
}

#define get_arg_angle get_arg_dim
#define get_arg_scale get_arg_dim

static void get_arg_dim(
    cp_dim_t *v,
    char const *arg,
    char const *str)
{
    char *r = NULL;
    *v = strtod(str, &r);
    if ((str == r) || (*r != '\0')) {
        fprintf(stderr, "Error: %s: invalid number: '%s'\n", arg, str);
        my_exit(1);
    }
}

static void get_arg_size(
    size_t *v,
    char const *arg,
    char const *str)
{
    char *r = NULL;
    unsigned long long val = strtoull(str, &r, 10);
    if ((str == r) || (*r != '\0')) {
        fprintf(stderr, "Error: %s: invalid number: '%s'\n", arg, str);
        my_exit(1);
    }
    *v = val & CP_MAX_OF(*v);
}

static void get_arg_uint8(
    unsigned char *v,
    char const *arg,
    char const *str)
{
    size_t v2;
    get_arg_size(&v2, arg, str);
    if (v2 > 255) {
        fprintf(stderr, "Error %s: invalid color value: %s expected 0..255\n", arg, str);
        my_exit(1);
    }
    *v = v2 & 0xff;
}

static void get_arg_rgb(
    cp_color_rgb_t *v,
    char const *arg,
    char const *str)
{
    char *r = NULL;
    unsigned long w = strtoul(str, &r, 16);
    if ((str == r) || (*r != '\0')) {
        fprintf(stderr, "Error: %s: invalid rgb color: '%s'\n", arg, str);
        my_exit(1);
    }

    v->r = (w >> 16) & 0xff;
    v->g = (w >> 8)  & 0xff;
    v->b = (w >> 0)  & 0xff;
}

typedef struct {
    char const *name;
    void (*func)(cp_opt_t *, char const *, char const *);
    unsigned need_arg;
} cp_get_opt_t;

#include "opt.inc"

typedef struct {
    char const *s;
    size_t n;
} cp_string_n_t;

static int opt_cmp(void const *_a, void const *_b)
{
    cp_string_n_t const *a = _a;
    cp_get_opt_t const *b = _b;
    int i = strncmp(a->s, b->name, a->n);
    if (i != 0) {
        return i;
    }
    if (b->name[a->n] != '\0') {
        return -1;
    }
    return 0;
}

static void parse_opt(
    cp_opt_t *opt,
    int *i,
    int argc,
    char **argv)
{
    char const *argvi = argv[*i];

    cp_string_n_t key = { .s = argvi };
    while (*key.s == '-') {
        key.s++;
    }
    key.n = strlen(key.s);
    char const *eq = strchr(key.s, '=');
    if (eq != NULL) {
        key.n = CP_PTRDIFF(eq, key.s);
    }

    cp_get_opt_t *g =
        bsearch(&key, opt_list, cp_countof(opt_list), sizeof(opt_list[0]), opt_cmp);
    if (g == NULL) {
        fprintf(stderr, "Error: Unrecognised option: '%s'\n", argvi);
        my_exit(1);
    }

    char const *arg = NULL;
    if (g->need_arg > 0) {
        if (key.s[key.n] == '=') {
            arg = key.s + key.n + 1;
        }
        else
        if (g->need_arg == 2) {
            if ((*i + 1) >= argc) {
                fprintf(stderr, "Error: Expected argument for '%s'\n", argvi);
                my_exit(1);
            }
            arg = argv[++*i];
        }
    }

    g->func(opt, argvi, arg);
}

static bool has_suffix(
    char const *haystack,
    char const *needle)
{
    size_t len1 = strlen(haystack);
    size_t len2 = strlen(needle);
    if (len1 < len2) {
        return false;
    }
    return strequ(haystack + len1 - len2, needle);
}

int main(int argc, char **argv)
{
    /* init options */
    cp_opt_t opt = {0};
    opt.z_step = 0.2;
    opt.z_max = -1;
    cp_mat4_unit(&opt.ps.xform2);
    opt.ps.color_path   = (cp_color_rgb_t){ .rgb = {   0,   0,   0 }};
    opt.ps.color_tri    = (cp_color_rgb_t){ .rgb = { 102, 102, 102 }};
    opt.ps.color_fill   = (cp_color_rgb_t){ .rgb = { 204, 204, 204 }};
    opt.ps.color_vertex = (cp_color_rgb_t){ .rgb = { 255,   0,   0 }};
    opt.ps.color_mark   = (cp_color_rgb_t){ .rgb = {   0,   0, 255 }};
    opt.ps.line_width = 0.4;
    opt.csg.max_fn = 100;
    opt.csg.layer_gap = -1;
    opt.csg.max_simultaneous = CP_CSG2_MAX_LAZY;
    opt.csg.optimise = CP_CSG2_OPT_DEFAULT;
    opt.csg.color_rand = 0;
    opt.verbose = 1;

    /* parse command line */
    char const *in_file_name = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            parse_opt(&opt, &i, argc, argv);
        }
        else
        if (in_file_name == NULL) {
            in_file_name = argv[i];
        }
        else {
            fprintf(stderr, "Error: Multiple input files cannot be processed: '%s'\n",
                argv[i]);
            my_exit(1);
        }
    }

    /* post-process options */
    if (cp_eq_epsilon > cp_pt_epsilon) {
        cp_eq_epsilon = cp_pt_epsilon;
    }
    if (cp_sqr_epsilon > cp_eq_epsilon) {
        cp_sqr_epsilon = cp_eq_epsilon;
    }
    if (!cp_eq(opt.ps_persp,0)) {
        cp_mat4_t m;
        cp_mat4_unit(&m);
        m.m[3][2] = opt.ps_persp / -1000.0;
        cp_mat4_mul(&opt.ps.xform2, &m, &opt.ps.xform2);
    }
#ifdef PSTRACE
    cp_debug_ps_opt = &opt.ps;
    cp_ps_xform_from_bb(&cp_debug_ps_xform, -100, -100, +100, +100);
    cp_debug_ps_xform.add_x -= CP_PS_PAPER_X/2;
    cp_debug_ps_xform.add_y -= CP_PS_PAPER_Y/2;
    cp_debug_ps_xform.add_x *= cp_debug_ps_scale_x;
    cp_debug_ps_xform.add_y *= cp_debug_ps_scale_y;
    cp_debug_ps_xform.mul_x *= cp_debug_ps_scale_x;
    cp_debug_ps_xform.mul_y *= cp_debug_ps_scale_y;
    cp_debug_ps_xform.add_x += CP_PS_PAPER_X/2;
    cp_debug_ps_xform.add_y += CP_PS_PAPER_Y/2;
    cp_debug_ps_xform.add_x += (cp_debug_ps_xlat_x * cp_debug_ps_xform.mul_x);
    cp_debug_ps_xform.add_y += (cp_debug_ps_xlat_y * cp_debug_ps_xform.mul_y);
#endif

    /* output file: */
    cp_stream_t *sout = CP_STREAM_FROM_FILE(stdout);
    FILE *fout = NULL;
    if (opt.out_file_name) {
        fout = fopen(opt.out_file_name, "wt");
        if (fout == NULL) {
            fprintf(stderr, "Error: Unable to open '%s' for writing: %s\n",
                opt.out_file_name, strerror(errno));
            my_exit(1);
        }
        sout = CP_STREAM_FROM_FILE(fout);

        if (!opt.have_dump) {
            if (has_suffix(opt.out_file_name, ".stl")) {
                opt.dump_stl = true;
            }
            else if (has_suffix(opt.out_file_name, ".js")) {
                opt.dump_js = true;
            }
            else if (
                has_suffix(opt.out_file_name, ".scad") ||
                has_suffix(opt.out_file_name, ".csg"))
            {
                opt.dump_csg2 = true;
            }
            else if (has_suffix(opt.out_file_name, ".ps")) {
                opt.dump_ps = true;
            }
            else {
                fprintf(stderr, "Error: Unrecognised file ending: '%s'.  Use --dump-...\n",
                    opt.out_file_name);
                my_exit(1);
            }
        }
    }

    /* process files */
    FILE *fin = fopen(in_file_name, "rt");
    if (fin == NULL) {
        fprintf(stderr, "Error: Unable to open '%s' for reading: %s\n",
            in_file_name, strerror(errno));
        my_exit(1);
    }

    cp_syn_tree_t *r = CP_NEW(*r);
    bool ok = do_file(sout, &opt, r, in_file_name, fin);
    fclose(fin);

    if (fout != NULL) {
        fclose(fout);
    }

    /* print error (FIXME: make this readable) */
    if (!ok) {
        cp_vchar_t pre, post;
        cp_syn_format_loc(&pre, &post, r, r->err.loc, r->err.loc2);

        if (r->err.msg.size == 0) {
            cp_vchar_printf(&r->err.msg, "Unknown failure.\n");
        }
        if (r->err.msg.data[r->err.msg.size-1] != '\n') {
            cp_vchar_push(&r->err.msg, '\n');
        }
        fprintf(stderr, "%sError: %s%s", pre.data, r->err.msg.data, post.data);
        my_exit(1);
    }

    my_exit(0);
}
