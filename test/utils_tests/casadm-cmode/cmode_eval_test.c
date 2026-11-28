/*
 * Copyright(c) 2026 Unvertical
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Unit test for casadm/cas_cmode_eval.c. Build and run from the repo root:
 *   cc -Wall -Icasadm -o /tmp/cmode_test test/utils_tests/casadm-cmode/cmode_eval_test.c casadm/cas_cmode_eval.c && /tmp/cmode_test
 */
#include <stdio.h>
#include "cas_cmode_eval.h"

#define WB 1
#define WT 0
#define PT 4	/* values are opaque to the evaluator */
#define NONE -1
#define CW PT	/* cache-wide mode used by all cases */

struct tcase {
	const char *what;
	struct cmode_class cls[6];
	size_t n;
	uint64_t core;
	int want;
	bool determined;
};

#define C0(m) { 0, "unclassified", m }
#define N(a) (sizeof(a) / sizeof((a)[0]))

static const struct tcase cases[] = {
	/* production style: several "cN|cM&done" classes plus class 0 */
	{ "prod: core 2 in class 1", { C0(WT), {1, "core_id:eq:2|core_id:eq:6&done", WB},
	  {2, "core_id:eq:3|core_id:eq:7&done", PT} }, 3, 2, WB, true },
	{ "prod: core 6 in class 1", { C0(WT), {1, "core_id:eq:2|core_id:eq:6&done", WB},
	  {2, "core_id:eq:3|core_id:eq:7&done", PT} }, 3, 6, WB, true },
	{ "prod: core 7 in class 2", { C0(WT), {1, "core_id:eq:2|core_id:eq:6&done", WB},
	  {2, "core_id:eq:3|core_id:eq:7&done", PT} }, 3, 7, PT, true },
	{ "prod: core 9 unclassified -> class 0", { C0(WT), {1, "core_id:eq:2|core_id:eq:6&done", WB},
	  {2, "core_id:eq:3|core_id:eq:7&done", PT} }, 3, 9, WT, true },
	{ "prod: no class 0 override -> cache-wide", { C0(NONE), {1, "core_id:eq:2|core_id:eq:6&done", WB} },
	  2, 9, CW, true },
	/* reviewer 1: class 0 override must not leak when a non-core class exists */
	{ "class 0 vs class with non-core condition", { C0(WT), {1, "core_id:eq:2&done", WB},
	  {2, "metadata", PT} }, 3, 5, CW, false },
	/* reviewer 2: later match wins when there is no done */
	{ "later match without done wins", { C0(NONE), {1, "core_id:eq:2", WB},
	  {2, "core_id:eq:2", WT} }, 3, 2, WT, true },
	{ "done stops before later match", { C0(NONE), {1, "core_id:eq:2&done", WB},
	  {2, "core_id:eq:2", WT} }, 3, 2, WB, true },
	/* reviewer 3: core id above 65535 must not alias */
	{ "core 65538 does not alias 2", { C0(NONE), {1, "core_id:eq:65538&done", WB} },
	  2, 2, CW, true },
	/* left to right, no precedence: (c2 & done) | c6 */
	{ "c2&done|c6: core 2 stops", { C0(NONE), {1, "core_id:eq:2&done|core_id:eq:6", WB},
	  {2, "core_id:eq:2", WT} }, 3, 2, WB, true },
	{ "c2&done|c6: core 6 not matched", { C0(NONE), {1, "core_id:eq:2&done|core_id:eq:6", WB} },
	  2, 6, CW, true },
	/* a class without override still captures the I/O */
	{ "override-less class captures core", { C0(WT), {1, "core_id:eq:4&done", NONE} },
	  2, 4, CW, true },
	{ "override-less class captures, later class loses", { C0(NONE), {1, "core_id:eq:4&done", NONE},
	  {2, "core_id:eq:4", WB} }, 3, 4, CW, true },
	/* unknown conditions */
	{ "unknown after a stopping class is harmless", { C0(NONE), {1, "core_id:eq:4&done", WB},
	  {2, "metadata", WT} }, 3, 4, WB, true },
	{ "unknown before a stopping class", { C0(NONE), {1, "metadata", WT},
	  {2, "core_id:eq:4&done", WB} }, 3, 4, WB, true },
	{ "unknown class without done before core class", { C0(NONE), {1, "core_id:eq:4&done", WB},
	  {2, "extension:tmp", WT} }, 3, 5, CW, false },
	{ "unknown conjunct: core_id:eq:4&metadata", { C0(WT), {1, "core_id:eq:4&metadata&done", WB} },
	  2, 4, CW, false },
	{ "unknown conjunct, other core is certain", { C0(WT), {1, "core_id:eq:4&metadata&done", WB} },
	  2, 5, WT, true },
	{ "unknown disjunct then done", { C0(WT), {1, "metadata|core_id:eq:4&done", WB} },
	  2, 4, WB, true },
	/* other numeric operators */
	{ "core_id:lt:3", { C0(NONE), {1, "core_id:lt:3&done", WB} }, 2, 2, WB, true },
	{ "core_id:ge:3", { C0(NONE), {1, "core_id:ge:3&done", WB} }, 2, 2, CW, true },
	{ "core_id:5 plain number is eq", { C0(NONE), {1, "core_id:5&done", WB} }, 2, 5, WB, true },
};

int main(void)
{
	int failed = 0;

	for (size_t i = 0; i < N(cases); i++) {
		const struct tcase *t = &cases[i];
		bool det;
		int got = cmode_effective(t->cls, t->n, t->core, CW, &det);

		if (got != t->want || det != t->determined) {
			printf("FAIL %s: got %d det=%d, want %d det=%d\n", t->what,
			       got, det, t->want, t->determined);
			failed++;
		}
	}
	printf("%zu cases, %d failed\n", N(cases), failed);
	return failed != 0;
}
