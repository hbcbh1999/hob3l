/* -*- Mode: C -*- */
/* Copyright (C) 2018 by Henrik Theiling, License: GPLv3, see LICENSE file */

#ifndef __CP_CSG3_H
#define __CP_CSG3_H

#include <hob3lbase/stream.h>
#include <hob3l/csg3_tam.h>
#include <hob3l/scad_tam.h>
#include <hob3l/csg3-2scad.h>

/** Create a CSG3 instance */
#define cp_csg3_new(r, _loc) \
    ({ \
        __typeof__(r) * __r = CP_NEW(*__r); \
        __r->type = cp_csg3_typeof(*__r); \
        __r->loc = (_loc); \
        __r; \
    })

/** Create a CSG3 object instance */
#define cp_csg3_new_obj(r, _loc, _gc) \
    ({ \
        __typeof__(r) * __rA = cp_csg3_new(r, _loc); \
        __rA->gc = (_gc); \
        __rA; \
    })

/** Specialising cast w/ dynamic check */
#define cp_csg3_cast(t,s) \
    ({ \
        assert((s)->type == cp_csg3_typeof(*(t))); \
        (__typeof__(t))(s); \
    })

/**  Generalising case w/ static check */
#define cp_csg3(t) \
    ({ \
        cp_static_assert(cp_csg3_typeof(*(t)) != 0); \
        (cp_csg3_t*)(t); \
    })

/**
 * Get bounding box of all points, including those that are
 * in subtracted parts that will be outside of the final solid.
 *
 * bb will not be cleared, but only updated.
 */
extern void cp_csg3_tree_max_bb(
    cp_vec3_minmax_t *bb,
    cp_csg3_tree_t const *r);

/**
 * Convert a SCAD AST into a CSG3 tree.
 */
extern bool cp_csg3_from_scad_tree(
    cp_csg3_tree_t *r,
    cp_err_t *t,
    cp_scad_tree_t *scad);

/* Dynamic casts */
CP_DECLARE_CAST_(csg3, sphere, CP_CSG3_SPHERE)
CP_DECLARE_CAST_(csg3, cyl,    CP_CSG3_CYL)
CP_DECLARE_CAST_(csg3, poly,   CP_CSG3_POLY)
CP_DECLARE_CAST_(csg3, add,    CP_CSG3_ADD)
CP_DECLARE_CAST_(csg3, sub,    CP_CSG3_SUB)
CP_DECLARE_CAST_(csg3, cut,    CP_CSG3_CUT)
CP_DECLARE_CAST_(csg3, 2d,     CP_CSG3_2D)

#endif /* __CP_CSG3_H */
