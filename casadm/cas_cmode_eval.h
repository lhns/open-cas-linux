/*
 * Copyright(c) 2025 Huawei Technologies
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CAS_CMODE_EVAL_H
#define CAS_CMODE_EVAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One existing IO class. Arrays are passed in ascending id order. */
struct cmode_class {
	unsigned id;
	const char *rule;	/* class name = classifier rule; class 0 has none */
	int mode;		/* cache-mode override, or -1 for none */
};

/* Effective cache mode of I/O to core @core_id, as the kernel classifier
 * (modules/cas_cache/classifier.c) and OCF resolve it: the last matching
 * class wins unless an earlier one hit "done"; its override applies, else
 * @cache_wide. Only core_id and done are known here; any other condition is
 * treated as unknown. *@determined is false when an unknown could change the
 * winning class; the result is then @cache_wide. */
int cmode_effective(const struct cmode_class *cls, size_t n, uint64_t core_id,
		    int cache_wide, bool *determined);

#endif
