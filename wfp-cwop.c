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
#include "wfp.h"

extern void send_url(char *host, int port, char *url, char *ident, int resp);
extern char *time_stamp(int gmt, int mode);
extern struct service_info sinfo[6];
extern double TempF(double c);
extern double MS2MPH(double ms);
extern double mb2in(double mb);

extern int debug;
extern int verbose;

static weather_data_t ws;
static int count = 0;

/*
 * CWOP publisher
 *
 * Unlike other services, publishing to CWOP involves sending a
 * formated string directly over tcp.  Not http protocol invloved.
 *
 * CWOP also limits the frequency that data can be sent to a
 * minimum of 10 minutes.  This will batch up the request
 * and send 'average' data at 10 minute intervals.
 */
void *send_to_cwop(void *data)
{
	weather_data_t *wd = (weather_data_t *)data;
	char *request;
	struct timeval start, end;
	char *ts_start, *ts_end;
	time_t t = time(NULL);
	struct tm* lt = localtime(&t);
	struct tm* gm = gmtime(&t);
	int humidity;
	char ident[50];

	/* First, is it time to send? */
	if ((lt->tm_min % 10) != 0) {
		/* would like to create average */
		ws.pressure += (wd->pressure * 338.637526);         /* in millibars * 10 */
		//ws.uncalibrated += (wd->uncalibrated * 338.637526);
		ws.windspeed += wd->windspeed;
		ws.winddirection += wd->winddirection;
		ws.temperature += wd->temperature;
		ws.humidity += wd->humidity;
		if (wd->gustspeed > ws.gustspeed)
			ws.gustspeed = wd->gustspeed;
		ws.rainfall_1hr = wd->rainfall_1hr;
		ws.rainfall_day = wd->rainfall_day;
		count++;
		free(wd);
		pthread_exit(NULL);
		return NULL;
	}

	/* ignore case where first data comes right on 10 minute */
	if (count == 0) {
		printf("** Skipping CWOP send, count == 0\n");
		free(wd);
		pthread_exit(NULL);
		return NULL;
	}

	gettimeofday(&start, NULL);

	if (verbose || debug) {
		ts_start = time_stamp(0, 1);
		fprintf(stderr, "%s: Begin upload to APRSWXNET\n", ts_start);
		free(ts_start);
	}

	/* Humidity needs some special handling */
	humidity = (int)round(ws.humidity / count);
	if (humidity == 100)
		humidity = 0;

	/*
	 * There are other data limitations that should be accounted
	 * for. Like rainfall per day can't be more than 9.99 inches.
	 */

	request = (char *)malloc(4000);
	sprintf(request, "%s>APRS,TCPIP*:/%02d%02d%02dz"
			"%s/%s"  /* lat / long */
			"_%03d"  /* wind direction 00 is north */
			"/%03d"  /* avg wind speed mph */
			"g%03d"  /* gust speed, mph */
			"t%03d"  /* temperature F */
			"r%03d"  /* rain in last hour in hundreths of inch */
			"P%03d"  /* rain since midnight in hundreths of inch */
			"h%02d"  /* humidity, 00 = 100% */
			"b%05d"  /* barometric pressure in tenths of millibars (uncorrected)*/
			"400\r\n",  /* hardware type */

			sinfo[CWOP].name,
			gm->tm_mday, gm->tm_hour, gm->tm_min,
			sinfo[CWOP].location_lat, sinfo[CWOP].location_long,
			(int)round(ws.winddirection / count),
			(int)round(ws.windspeed / count),
			(int)round(ws.gustspeed),
			(int)round(ws.temperature / count),
			(int)round(ws.rainfall_1hr * 100),
			(int)round(ws.rainfall_day * 100),
			humidity,
			(int)round(ws.pressure / count)
			);

	if (verbose > 1)
		fprintf(stderr, "CWOP: %s\n", request);


	sprintf(ident, "user %s pass -1 vers linux-acu-link 1.00\r\n", sinfo[CWOP].name);
	send_url(sinfo[CWOP].host, 14580, request, ident, 0);

	/* Open a socket and send the data */
	free(request);
	free(data);

	/* Clear data */
	count = 0;
	memset(&ws, 0, sizeof(weather_data_t));

	gettimeofday(&end, NULL);
	if (verbose || debug) {
		long diff;

		diff = ((end.tv_sec-start.tv_sec)*1000000 +
				(end.tv_usec-start.tv_usec)) / 1000;
		ts_end = time_stamp(0, 1);

		fprintf(stderr, "%s: Upload to CWOP complete in %ld msecs\n",
				ts_end, diff);
		free(ts_end);
	}

	pthread_exit(NULL);
	return NULL;
}
