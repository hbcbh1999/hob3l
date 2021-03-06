/* -*- Mode: C -*- */
/* Copyright (C) 2018 by Henrik Theiling, License: GPLv3, see LICENSE file */

#ifndef __CP_POOL_TAM_H
#define __CP_POOL_TAM_H

#include <stddef.h>

typedef struct cp_pool_block cp_pool_block_t;

typedef struct {
    cp_pool_block_t *cur;
    size_t block_size;
} cp_pool_t;

#endif /*__CP_POOL_H */
