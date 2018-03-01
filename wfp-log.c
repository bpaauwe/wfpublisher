/*
 * Copyright (c) 2018 Robert Paauwe
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software")
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "wfp.h"

extern char *time_stamp(int gmt, int mode);

static int debug;

/*
 * Local log to file
 *
 * Log the weather data to a local file on the filesystem
 */
void send_to_log(struct cfg_info *cfg, struct station_info *station,
				weather_data_t *wd)
{
	struct timeval start, end;
	char *ts_start, *ts_end;
	time_t t = time(NULL);
	struct tm* lt = localtime(&t);
	FILE *fp;
	char p_str[5];
	char m_str[4];

	gettimeofday(&start, NULL);

	if (!cfg->metric) {
		unit_convert(wd, CONVERT_ALL);
		sprintf(p_str, "HgIn");
		sprintf(m_str, "mph");
	} else {
		sprintf(p_str, "mb");
		sprintf(m_str, "m/s");
	}

	fp = fopen(cfg->host, "a");
	if (fp == NULL) {
		fprintf(stderr, "Failed to open file %s for writing\n", cfg->host);
		goto end;
	}

	if (debug) {
		ts_start = time_stamp(0, 1);
		fprintf(stderr, "%s: Begin logging to %s\n", ts_start, cfg->host);
		free(ts_start);
	}

	fprintf(fp, "%4d-%02d-%02d %02d:%02d:%02d",
			lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
			lt->tm_hour, lt->tm_min, lt->tm_sec);

	fprintf(fp, "|%.2f%s|%.2f|%.2f|%.2f|%.2f|%.2f|%3.0f|%.1f%s|%.1f%s|%.1f%%|%.1fº|%.1fº\n",
			wd->pressure,
			p_str,
			wd->rainfall_year,
			wd->rainfall_month,
			wd->rainfall_day,
			wd->rainfall_1hr,
			wd->rain,
			wd->winddirection,
			wd->gustspeed,
			m_str,
			wd->windspeed,
			m_str,
			wd->humidity,
			wd->dewpoint,
			wd->temperature);

	fclose (fp);

end:

	gettimeofday(&end, NULL);
	if (debug) {
		long diff;

		diff = ((end.tv_sec-start.tv_sec)*1000000 +
				(end.tv_usec-start.tv_usec)) / 1000;
		ts_end = time_stamp(0, 1);

		fprintf(stderr, "%s: Log file update complete in %ld msecs\n",
				ts_end, diff);
		free(ts_end);
	}

	return;
}

static int log_init(struct cfg_info *cfg, int d)
{
	debug = d;
	return 0;
}

static const struct publisher_funcs log_funcs = {
	.init = log_init,
	.update = send_to_log,
	.cleanup = NULL
};

void log_setup(struct service_info *sinfo)
{
	sinfo->funcs = log_funcs;
	return;
}

