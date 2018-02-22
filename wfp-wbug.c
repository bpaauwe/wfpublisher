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

static int debug;
static char *tpl = "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n";
static weather_data_t ws;
static int count = 0;

/*
 * WeatherBug publisher
 */
void send_to_weatherbug(struct cfg_info *cfg, struct station_info *station,
						weather_data_t *wd)
{
	char *str;
	char *request;
	struct timeval start, end;
	char *ts_start, *ts_end;
	time_t t = time(NULL);
	struct tm* lt = localtime(&t);

	if (!cfg->metric)
		unit_convert(wd, CONVERT_ALL);

	/* Limit sending to weatherbug */
	if ((lt->tm_min % 2) != 0) {
		/* Average data if called more frequently than limit */
		ws.pressure      += wd->pressure;
		ws.windspeed     += wd->windspeed;
		ws.winddirection += wd->winddirection;
		if (wd->gustspeed > ws.gustspeed)
			ws.gustspeed      = wd->gustspeed;
		ws.gustdirection  = wd->gustdirection;
		ws.humidity      += wd->humidity;
		ws.dewpoint      += wd->dewpoint;
		ws.temperature   += wd->temperature;
		ws.rainfall_day   = wd->rainfall_day;
		ws.rainfall_1hr   = wd->rainfall_1hr;
		ws.rainfall_month = wd->rainfall_month;
		ws.rainfall_year  = wd->rainfall_year;
		count++;
		goto out;
	}

	if (count == 0)
		goto out;

	gettimeofday(&start, NULL);

	if (debug) {
		ts_start = time_stamp(0, 1);
		fprintf(stderr, "%s: Begin upload to WeatherBug\n", ts_start);
		free(ts_start);
	}

	ts_start = time_stamp(1, 0);
	request = (char *)malloc(1024);
	sprintf(request, "data/livedata.aspx?"
			"action=live"
			"&ID=%s"
			"&Key=%s"
			"&Num=%s"
			"&dateutc=%s"
			"&softwaretype=Experimental"
			"&baromin=%f"
			"&dailyrainin=%f"
			"&rainin=%f"
			"&windgustdir=%f"
			"&winddir=%f"
			"&windgustmph=%f"
			"&windspeedmph=%f"
			"&humidity=%f"
			"&dewptf=%f"
			"&tempf=%f"
			"&monthlyrainin=%.2f"
			"&Yearlyrainin=%.2f",
			cfg->name,
			cfg->pass,
			cfg->extra,
			ts_start,
			(ws.pressure / count),
			(ws.rainfall_day),
			(ws.rainfall_1hr),
			(ws.gustdirection),
			(ws.winddirection / count),
			(ws.gustspeed),
			(ws.windspeed / count),
			(ws.humidity / count),
			(ws.dewpoint / count),
			(ws.temperature / count),
			(ws.rainfall_month),
			(ws.rainfall_year)
			);

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

	count = 0;
	memset(&ws, 0, sizeof(weather_data_t));

	gettimeofday(&end, NULL);
	if (debug) {
		long diff;

		diff = ((end.tv_sec-start.tv_sec)*1000000 +
				(end.tv_usec-start.tv_usec)) / 1000;
		ts_end = time_stamp(0, 1);

		fprintf(stderr, "%s: Upload to WeatherBug complete in %ld msecs\n",
				ts_end, diff);
		free(ts_end);
	}

out:
	pthread_exit(NULL);
	return;
}

static int wbug_init(struct cfg_info *cfg, int d)
{
	debug = d;
	return 0;
}


static const struct publisher_funcs wbug_funcs = {
	.init = wbug_init,
	.update = send_to_weatherbug,
	.cleanup = NULL
};

void wbug_setup(struct service_info *sinfo)
{
	sinfo->funcs = wbug_funcs;
	return;
}

