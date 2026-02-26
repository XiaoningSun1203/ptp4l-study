/**
 * @file clockcheck.c
 * @note Copyright (C) 2013 Miroslav Lichvar <mlichvar@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <time.h>

#include "clockcheck.h"
#include "print.h"

#define CHECK_MIN_INTERVAL 100000000
#define CHECK_MAX_FREQ 900000000

struct clockcheck {
	/* Sanity frequency limit */
	int freq_limit;
	/* Frequency was set at least once */
	int freq_known;
	/* Current frequency */
	int current_freq;
	/* Maximum and minimum frequency since last update */
	int max_freq;
	int min_freq;
	uint64_t last_ts;
	uint64_t last_mono_ts;
};

struct clockcheck *clockcheck_create(int freq_limit)
{
	struct clockcheck *cc;
	cc = calloc(1, sizeof(*cc));
	if (!cc)
		return NULL;
	cc->freq_limit = freq_limit;
	cc->max_freq = -CHECK_MAX_FREQ;
	cc->min_freq = CHECK_MAX_FREQ;
	return cc;
}

// 判断PHC有没有突然走得太快、太慢，或者直接跳了一大步。
// 如果 PHC 突然：向前跳 1 秒/向后跳 500ms/频率瞬间暴走,那 servo 继续工作是危险的
int clockcheck_sample(struct clockcheck *cc, uint64_t ts)
{
	uint64_t mono_ts;
	int64_t interval, mono_interval;
	double max_foffset, min_foffset;
	struct timespec now;
	int ret = 0;

	/* Check the sanity of the synchronized clock by comparing its
	   uncorrected frequency with the system monotonic clock. If
	   the synchronized clock is the system clock, the measured
	   frequency offset will be the current frequency correction of
	   the system clock. */

	if (!cc->freq_known)
		return ret;

	interval = (int64_t)ts - cc->last_ts;  // 计算两次采样之间的间隔
	if (interval >= 0 && interval < CHECK_MIN_INTERVAL)
		return ret;

	clock_gettime(CLOCK_MONOTONIC, &now);  // mono_interval = 两次检查之间，系统单调时钟（CLOCK_MONOTONIC）走了多少纳秒
	mono_ts = now.tv_sec * 1000000000LL + now.tv_nsec;
	mono_interval = (int64_t)mono_ts - cc->last_mono_ts;

	if (mono_interval < CHECK_MIN_INTERVAL)
		return ret;

	if (cc->last_ts && cc->max_freq <= CHECK_MAX_FREQ) {
		max_foffset = 1e9 * (interval /
				     (1.0 + cc->min_freq / 1e9) /
				     mono_interval - 1.0);
		min_foffset = 1e9 * (interval /
				     (1.0 + cc->max_freq / 1e9) /
				     mono_interval - 1.0);

		if (min_foffset > cc->freq_limit) {
			pr_warning("clockcheck: clock jumped forward or"
					" running faster than expected!");
			ret = 1;
		} else if (max_foffset < -cc->freq_limit) {
			pr_warning("clockcheck: clock jumped backward or"
					" running slower than expected!");
			ret = 1;
		}
	}

	cc->last_mono_ts = mono_ts;
	cc->last_ts = ts;
	cc->max_freq = cc->min_freq = cc->current_freq;

	return ret;
}

void clockcheck_set_freq(struct clockcheck *cc, int freq)
{
	if (cc->max_freq < freq)
		cc->max_freq = freq;
	if (cc->min_freq > freq)
		cc->min_freq = freq;
	cc->current_freq = freq;
	cc->freq_known = 1;
}

void clockcheck_step(struct clockcheck *cc, int64_t step)
{
	if (cc->last_ts)
		cc->last_ts += step;
}

void clockcheck_destroy(struct clockcheck *cc)
{
	free(cc);
}
