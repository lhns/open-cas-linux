/*
 * Copyright(c) 2026 Unvertical
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "cas_cmode_eval.h"

/* Possible (match, stop) outcomes of a rule, as a bit set */
#define OUT(yes, stop) (1u << ((yes) * 2 + (stop)))

enum tri { T_NO, T_YES, T_UNKNOWN };

/* core_id:[op:]N against @core; anything malformed is unknown */
static enum tri core_id_cond(const char *operand, uint64_t core)
{
	const char *num = operand;
	char op[3] = "eq";
	char *end;
	uint64_t v;

	if (!operand || !*operand)
		return T_UNKNOWN;
	if (strchr(operand, ':')) {
		if (operand[2] != ':')
			return T_UNKNOWN;
		memcpy(op, operand, 2);
		op[2] = '\0';
		num = operand + 3;
	}
	if (*num < '0' || *num > '9')
		return T_UNKNOWN;
	errno = 0;
	v = strtoull(num, &end, 10);
	if (errno || *end)
		return T_UNKNOWN;

	if (!strcmp(op, "eq"))
		return core == v ? T_YES : T_NO;
	if (!strcmp(op, "ne"))
		return core != v ? T_YES : T_NO;
	if (!strcmp(op, "lt"))
		return core < v ? T_YES : T_NO;
	if (!strcmp(op, "gt"))
		return core > v ? T_YES : T_NO;
	if (!strcmp(op, "le"))
		return core <= v ? T_YES : T_NO;
	if (!strcmp(op, "ge"))
		return core >= v ? T_YES : T_NO;
	return T_UNKNOWN;
}

/* Mirror of cas_cls_process_rule(): conditions combine left to right with no
 * precedence, an '&' while the result is false ends the rule, and "done"
 * (yes + stop) ends it when reached. Tracks every outcome the unknown
 * conditions allow. */
static unsigned rule_outcomes(const char *rule, uint64_t core)
{
	char *buf = strdup(rule), *p = buf;
	unsigned live = OUT(0, 0), fin = 0;	/* running / finished states */
	char lop = '|';

	if (!buf)
		return OUT(0, 0) | OUT(0, 1) | OUT(1, 0) | OUT(1, 1);

	while (*p && live) {
		char *tok = p, *operand = NULL, *q = strpbrk(p, ":&|");
		char next = 0;
		enum tri t = T_UNKNOWN;
		unsigned nlive = 0;
		bool is_done;

		if (q && *q == ':') {
			*q = '\0';
			operand = q + 1;
			q = strpbrk(operand, "&|");
		}
		if (q) {
			next = *q;
			*q = '\0';
			if (!q[1]) {	/* kernel rejects a trailing operator */
				fin |= OUT(0, 0) | OUT(0, 1) | OUT(1, 0) |
				       OUT(1, 1);
				live = 0;
				break;
			}
		}

		is_done = !strcmp(tok, "done");
		if (!strcmp(tok, "core_id"))
			t = core_id_cond(operand, core);

		for (unsigned y = 0; y < 2; y++) {
			if (!(live & OUT(y, 0)))
				continue;
			if (lop == '&' && !y) {
				fin |= OUT(0, 0);
				continue;
			}
			if (is_done) {
				fin |= OUT(1, 1);	/* done: yes + stop */
				continue;
			}
			for (unsigned r = 0; r < 2; r++) {
				if ((r && t == T_NO) || (!r && t == T_YES))
					continue;
				nlive |= OUT(lop == '&' ? (r && y) : (r || y), 0);
			}
		}
		live = nlive;
		lop = next;
		p = q ? q + 1 : p + strlen(p);
	}

	free(buf);
	return fin | live;
}

int cmode_effective(const struct cmode_class *cls, size_t n, uint64_t core_id,
		    int cache_wide, bool *determined)
{
	/* possible current winners: index into cls[], or n = unclassified */
	bool run[n + 1], done[n + 1];
	size_t w, k, found = 0, win = n;
	int mode = -1;

	memset(run, 0, sizeof(run));
	memset(done, 0, sizeof(done));
	run[n] = true;

	for (k = 0; k < n; k++) {
		bool nrun[n + 1];
		unsigned out;

		if (!cls[k].id || !cls[k].rule || !*cls[k].rule)
			continue;	/* no rule object in the kernel */
		out = rule_outcomes(cls[k].rule, core_id);
		memset(nrun, 0, sizeof(nrun));
		for (w = 0; w <= n; w++) {
			if (!run[w])
				continue;
			for (unsigned y = 0; y < 2; y++)
				for (unsigned s = 0; s < 2; s++) {
					size_t nw = y ? k : w;

					if (!(out & OUT(y, s)))
						continue;
					(s ? done : nrun)[nw] = true;
				}
		}
		memcpy(run, nrun, sizeof(run));
	}

	for (w = 0; w <= n; w++)
		if (run[w] || done[w]) {
			found++;
			win = w;
		}

	*determined = found == 1;
	if (found != 1)
		return cache_wide;
	if (win < n)
		mode = cls[win].mode;
	else
		for (k = 0; k < n; k++)
			if (cls[k].id == 0)
				mode = cls[k].mode;
	return mode >= 0 ? mode : cache_wide;
}
