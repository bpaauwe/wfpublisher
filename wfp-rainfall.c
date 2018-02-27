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
#include <math.h>
#include "cJSON.h"
#include "wfp.h"

extern int debug;
extern int verbose;

static void save_rainfall(weather_data_t *wd);

static double rain_60_min[60] = {0};
static double rain_24_hr[24] = {0};

/*
 * Rain data comes in at mm's over a 1 minute interval. Use
 * this to track rain over other timeframes.
 *
 * Save the accumulated rain values so that we can recover
 * from a restart.
 */
void accumulate_rain(weather_data_t *wd, double rain)
{
	time_t sec = time(NULL);
	struct tm *lt = localtime(&sec);
	int i;

	/* Every hour */
	wd->rainfall_1hr = (lt->tm_min == 0) ? rain : wd->rainfall_1hr + rain;

	/* day */
	if ((lt->tm_hour == 0) && (lt->tm_min == 0))
		wd->rainfall_day = rain;
	else
		wd->rainfall_day += rain;

	/* month */
	if ((lt->tm_mday == 1) && (lt->tm_hour == 0) && (lt->tm_min == 0))
		wd->rainfall_month = rain;
	else
		wd->rainfall_month += rain;

	/* year */
	if ((lt->tm_mon =- 0) && (lt->tm_mday == 1) && (lt->tm_hour == 0) && (lt->tm_min == 0))
		wd->rainfall_year = rain;
	else
		wd->rainfall_year += rain;

	rain_60_min[lt->tm_min] = rain;
	wd->rainfall_60min = 0;
	for (i = 0; i < 60; i++)
		wd->rainfall_60min += rain_60_min[i];

	rain_24_hr[lt->tm_hour] = wd->rainfall_1hr;
	wd->rainfall_24hr = 0;
	for (i = 0; i < 24; i++)
		wd->rainfall_24hr += rain_24_hr[i];

	/* Save current values */
	save_rainfall(wd);

}

static void save_rainfall(weather_data_t *wd)
{
	time_t t = time(NULL);
	struct tm lt;
	cJSON *rain;
	cJSON *l_time;
	FILE *fp;

	localtime_r(&t, &lt);

	l_time = cJSON_CreateObject();
	cJSON_AddNumberToObject(l_time, "hour", lt.tm_hour);
	cJSON_AddNumberToObject(l_time, "day", lt.tm_mday);
	cJSON_AddNumberToObject(l_time, "month", lt.tm_mon + 1);
	cJSON_AddNumberToObject(l_time, "year", lt.tm_year + 1900);

	rain = cJSON_CreateObject();
	cJSON_AddItemToObject(rain, "time", l_time);
	cJSON_AddNumberToObject(rain, "rain_60", wd->rainfall_60min);
	cJSON_AddNumberToObject(rain, "rain_24", wd->rainfall_24hr);
	cJSON_AddNumberToObject(rain, "rain_current_hour", wd->rainfall_1hr);
	cJSON_AddNumberToObject(rain, "rain_current_day", wd->rainfall_day);
	cJSON_AddNumberToObject(rain, "rain_current_month", wd->rainfall_month);
	cJSON_AddNumberToObject(rain, "rain_current_year", wd->rainfall_year);

	fp = fopen("rainfall.json", "w");
	if (fp < 0) {
		fprintf(stderr, "Failed to open rainfall.json for writing.\n");
	} else {
		fprintf(fp, "%s\n", cJSON_Print(rain));
		fclose(fp);
	}

	cJSON_Delete(rain);
}
