/* -*- Mode: C -*- */
/* Copyright (C) 2018 by Henrik Theiling, License: GPLv3, see LICENSE file */

#ifndef __CP_CSG2_H
#define __CP_CSG2_H

#include <hob3lbase/mat_tam.h>
#include <hob3l/obj.h>
#include <hob3l/csg2_tam.h>
#include <hob3l/csg3_tam.h>
#include <hob3l/csg2-bool.h>
#include <hob3l/csg2-layer.h>
#include <hob3l/csg2-triangle.h>
#include <hob3l/csg2-tree.h>
#include <hob3l/csg2-2ps.h>
#include <hob3l/csg2-2scad.h>
#include <hob3l/csg2-2stl.h>
#include <hob3l/csg2-2js.h>

/** Create a CSG2 instance */
#define cp_csg2_new(r, l) _cp_new(cp_csg2_typeof, r, l)

/** Cast w/ dynamic check */
#define cp_csg2_cast(t, s) _cp_cast(cp_csg2_typeof, t, s)

/** Cast w/ dynamic check */
#define cp_csg2_try_cast(t, s) _cp_try_cast(cp_csg2_typeof, t, s)

/**
 * Compute bounding box
 *
 * This uses only the points, neither the triangles nor the paths.
 *
 * Runtime: O(n), n=number of points
 */
static inline void cp_csg2_poly_minmax(
    cp_vec2_minmax_t *m,
    cp_csg2_poly_t *o)
{
    cp_v_vec2_loc_minmax(m, &o->point);
}

/**
 * Get a point of a path in a poly.
 */
static inline cp_vec2_loc_t *cp_csg2_path_nth(
    cp_csg2_poly_t *poly,
    cp_csg2_path_t *path,
    size_t i)
{
    assert(i < path->point_idx.size);
    size_t j = path->point_idx.data[i];
    assert(j < poly->point.size);
    return &poly->point.data[j];
}

#endif /* __CP_CSG2_H */
