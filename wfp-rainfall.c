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

extern int rainfall_data_save(double min, double hour, double day, double month, double year);
extern int rainfall_data_get(double *min, double *hour, double *day,
		double *month, double *year);

extern int debug;
extern int verbose;

typedef struct  __rfqueue {
	time_t seconds;
	double amount;
	struct __rfqueue *next;
} rainrec_t;

static double get_rainfall(rainrec_t *start);
static void init_rainfall(void);

static rainrec_t *head = NULL;
static rainrec_t *hour = NULL;
static rainrec_t *tail = NULL;

/* What about hourly, daily, monthly, yearly totals? */
static rainrec_t minutely;
static rainrec_t hourly;
static rainrec_t daily;
static rainrec_t monthly;
static rainrec_t yearly;

/*
 * Rainfall.
 *
 * Keep track of rainfall data. Call into this every
 * time we get rainfall data from the sensor.
 *
 * Will need way to get at the rainfall values for
 * various time periods.
 *
 * Need to have a rolling 24hr and 1hr queue of rainfall
 * data.
 */

void rainfall(double amount)
{
	rainrec_t *r;
	time_t sec = time(NULL);
	struct tm *lt = localtime(&sec);

	r = malloc(sizeof(rainrec_t));
	r->seconds = sec;
	r->amount = amount;
	r->next = NULL;

	if (!head) {
		init_rainfall();

		/* TODO: init should read and populate lists too */
		head = r;
		tail = r;
		hour = r;
	} else {
		/* Add to end of queue */
		tail->next = r;
		tail = r;
	}

	/* If head is older than 24hrs remove */
	while (head->seconds < (sec - (24 * 60 * 60))) {
		r = head->next;
		free (head);
		head = r;
	}

	/* Move hour pointer */
	while (hour->seconds < (sec - (60 * 60)))
		hour = hour->next;

	/* print some debug info now. */

	printf("Rainfall %ld:  %f @ %ld    %f @ %ld\n", sec, head->amount, head->seconds, hour->amount, hour->seconds);
	printf("   last hour     = %f\n", get_rainfall(hour));
	printf("   last 24 hours = %f\n", get_rainfall(head));


	/* Every minute */
	if ((sec - minutely.seconds) > 60) {
		minutely.seconds = sec;
		minutely.amount = 0;
	}
	minutely.amount += amount;

	/* Every hour */
	if (lt->tm_min == 0) {
		if ((sec - hourly.seconds) > 60) {
			hourly.seconds = sec;
			hourly.amount = 0;
		}
	}
	hourly.amount += amount;

	/* Every day */
	if ((lt->tm_hour == 0) && (lt->tm_min == 0)) {
		if ((sec - daily.seconds) > 60) {
			daily.seconds = sec;
			daily.amount = 0;
		}
	}
	daily.amount += amount;

	/* Every month */
	if ((lt->tm_mday == 1) && (lt->tm_hour == 0) && (lt->tm_min == 0)) {
		if ((sec - monthly.seconds) > 60) {
			monthly.seconds = sec;
			monthly.amount = 0;
		}
	}
	monthly.amount += amount;

	/* Every year */
	if ((lt->tm_mon =- 0) && (lt->tm_mday == 1) && (lt->tm_hour == 0) && (lt->tm_min == 0)) {
		if ((sec - yearly.seconds) > 60) {
			yearly.seconds = sec;
			yearly.amount = 0;
		}
	}
	yearly.amount += amount;

	/*
	 * Save current data
	 * TODO: Should this really be saving to the database?  Maybe we want to
	 * use a data file instead so that we don't have to rely on having a
	 * database configured.
	 * Would it be a bad idea to have this stored in the config file?
	 */
	//rainfall_data_save(minutely.amount, hourly.amount, daily.amount, monthly.amount, yearly.amount);

}

static void init_rainfall()
{
	minutely.seconds = 0;
	minutely.amount = 0;
	minutely.next = NULL;

	hourly.seconds = 0;
	hourly.amount = 0;
	hourly.next = NULL;

	daily.seconds = 0;
	daily.amount = 0;
	daily.next = NULL;

	monthly.seconds = 0;
	monthly.amount = 0;
	monthly.next = NULL;

	yearly.seconds = 0;
	yearly.amount = 0;
	yearly.next = NULL;

	/* TODO: Should this really query the database?
	rainfall_data_get(
			&minutely.amount,
			&hourly.amount,
			&daily.amount,
			&monthly.amount,
			&yearly.amount);
	*/
}


static double get_rainfall(rainrec_t *start)
{
	double amount = 0;

	while (start) {
		amount += start->amount;
		start = start->next;
	}

	return amount;
}


/* wrapper functions to get at the rain data */

double get_rain_minute(void)
{
	return get_rainfall(&minutely);
}
double get_rain_hourly(void)
{
	return get_rainfall(&hourly);
}
double get_rain_daily(void)
{
	return get_rainfall(&daily);
}
double get_rain_monthly(void)
{
	return get_rainfall(&monthly);
}
double get_rain_yearly(void)
{
	return get_rainfall(&yearly);
}
double get_rain_24hrs(void)
{
	return get_rainfall(head);
}
double get_rain_1hr(void)
{
	return get_rainfall(hour);
}

