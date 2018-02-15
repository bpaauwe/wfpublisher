/*
 * Copyright (c) 2013,2014 Robert Paauwe
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
#include "wfp.h"

extern void send_url(char *host, int port, char *url, char *ident, int resp);
extern char *time_stamp(int gmt, int mode);
extern double TempF(double c);
extern double MS2MPH(double ms);
extern double mb2in(double mb);

extern int debug;
extern int verbose;

static char *tpl = "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n";

/*
 * Weather Underground publisher.
 */
void send_to_wunderground(struct cfg_info *cfg, weather_data_t *wd)
{
	char *str;
	char *request;
	struct timeval start, end;
	char *ts_start, *ts_end;

	gettimeofday(&start, NULL);

	if (debug) {
		printf("In send_to_wunderground\n");
	}

	if (verbose || debug) {
		ts_start = time_stamp(0, 1);
		fprintf(stderr, "%s: Begin upload to WUnderground\n", ts_start);
		free(ts_start);
	}

	ts_start = time_stamp(1, 0);
	request = (char *)malloc(1024);
	sprintf(request, "weatherstation/updateweatherstation.php?"
			"ID=%s"
			"&PASSWORD=%s"
			"&dateutc=%s"
			"&softwaretype=Experimental"
			"&action=updateraw"
			"&baromin=%f"
			"&dailyrainin=%f"
			"&rainin=%f"
			"&windgustdir=%f"
			"&winddir=%f"
			"&windgustmph=%f"
			"&windspeedmph=%f"
			"&humidity=%f"
			"&dewptf=%f"
			"&tempf=%f",
			cfg->name,
			cfg->pass,
			ts_start,
			wd->pressure,
			wd->rainfall_day,
			wd->rain,
			wd->gustdirection,
			wd->winddirection,
			wd->gustspeed,
			wd->windspeed,
			wd->humidity,
			wd->dewpoint,
			wd->temperature
			);

	if (verbose > 1)
		fprintf(stderr, "wunderground: %s\n", request);

	/*
	 * Build url string using tpl as a template
	 * sprintf(query, tpl, <page>, <host>, <USERAGENT>)
	 *
	 * <page> is the full string with data
	 * <host> is the host name
	 * <USERAGENT is the user agent string
	 */

	str = (char *)malloc(4096);

	sprintf(str, tpl, request, cfg->host, "acu-link");
	if (!debug) {
		send_url(cfg->host, 80, str, NULL, 1);
	} else {
		send_url("www.bobshome.net", 80, str, NULL, 0);
	}

	free(ts_start);
	free(str);
	free(request);

	gettimeofday(&end, NULL);

	if (verbose || debug) {
		long diff;

		diff = ((end.tv_sec-start.tv_sec)*1000000 +
				(end.tv_usec-start.tv_usec)) / 1000;
		ts_end = time_stamp(0, 1);

		fprintf(stderr, "%s: Upload to WUnderground complete in %ld msecs\n",
				ts_end, diff);
		free(ts_end);
	}

	pthread_exit(NULL);
	return;
}

static const struct publisher_funcs wunderground_funcs = {
	.init = NULL,
	.update = send_to_wunderground,
	.cleanup = NULL
};

void wunderground_setup(struct service_info *sinfo)
{
	sinfo->funcs = wunderground_funcs;
	return;
}

