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

#define _GNU_SOURCE
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
#include <mysql/mysql.h>
#include "wfp.h"

extern char *time_stamp(int gmt, int mode);
extern struct service_info sinfo[6];
extern double TempF(double c);
extern double MS2MPH(double ms);
extern double mb2in(double mb);


extern int debug;
extern int verbose;

static int db_query(MYSQL *sql, char *query_str, char *msg);
static int connect_to_database(MYSQL *sql, char *db_host, char *db_name,
		char *db_user, char *db_pass);

/*
 * Store data in a MYSQL (or compatible) database
 */
void *send_to_db(void *data)
{
	weather_data_t *wd = (weather_data_t *)data;
	struct timeval start, end;
	char *ts_start, *ts_end;
	char *query;
	int ret;
	MYSQL *sql;

	gettimeofday(&start, NULL);

	if (verbose || debug) {
		ts_start = time_stamp(0, 1);
		fprintf(stderr, "%s: Begin database update to %s\n", ts_start, sinfo[0].host);
		free(ts_start);
	}

	/* Initialize and open connection to database */
	if ((sql = mysql_init(NULL)) == NULL) {
		fprintf(stderr, "Failed to initialize MySQL interface.\n");
		goto end;
	}

	ret = connect_to_database(sql, sinfo[DB_MYSQL].host, sinfo[DB_MYSQL].extra,
			sinfo[DB_MYSQL].name, sinfo[DB_MYSQL].pass);
	if (!ret)
		goto end;


	/* Generate the sql statment to update the database. */
	ret = asprintf(&query, "insert into weather_log set "
			"pressure=\"%f\","
			"temperature=\"%f\","
			"humidity=\"%f\","
			"windspeed=\"%f\","
			"winddirection=\"%f\","
			"gustspeed=\"%f\","
			"gustdirection=\"%f\","
			"rainfall_1min=\"%f\","
			"rainfall_1hr=\"%f\","
			"rainfall_day=\"%f\","
			"rainfall_month=\"%f\","
			"rainfall_year=\"%f\","
			"dewpoint=\"%f\","
			"heatindex=\"%f\"",
			mb2in(wd->pressure),
			TempF(wd->temperature),
			wd->humidity,
			MS2MPH(wd->windspeed),
			wd->winddirection,
			MS2MPH(wd->gustspeed),
			wd->rainfall_1min,
			wd->rainfall_1hr,
			wd->rainfall_day,
			wd->rainfall_month,
			wd->rainfall_year,
			wd->winddirection,
			TempF(wd->dewpoint),
			wd->heatindex);

	db_query(sql, query, "Failed to update record");
	free(query);

end:
	mysql_close(sql);
	free(data);

	gettimeofday(&end, NULL);
	if (verbose || debug) {
		long diff;

		diff = ((end.tv_sec-start.tv_sec)*1000000 +
				(end.tv_usec-start.tv_usec)) / 1000;
		ts_end = time_stamp(0, 1);

		fprintf(stderr, "%s: Database update complete in %ld msecs\n",
				ts_end, diff);
		free(ts_end);
	}

	pthread_exit(NULL);
	return NULL;
}

/*
 * Make a connection to the database server.
 */
static int connect_to_database(MYSQL *sql, char *db_host, char *db_name,
		char *db_user, char *db_pass)
{
	my_bool reconnect = 1;

	printf("Connecting to database %s:%s\n", db_host, db_name);
	mysql_options(sql, MYSQL_OPT_RECONNECT, &reconnect);

	if (!mysql_real_connect(sql, db_host, db_user, db_pass, NULL, 0, NULL, 0)) {
		fprintf(stderr, "Failed to connect to database: Error: %s\n",
				mysql_error(sql));
		return 0;
	}


	if (mysql_select_db(sql, db_name)) {
		fprintf(stderr, "Error selecting database: Error: %s\n",
				mysql_error(sql));
		return 0;
	}

	printf("database is now conencted\n");
	return 1;
}

/*
 * Make a query to the database.  Keep trying until the query succeeds.
 *
 * Should this fail if the query fails?  Can the flow abort gracefully
 * if this fails?
 */
static int db_query(MYSQL *sql, char *query_str, char *msg)
{
	if (sql == NULL)
		return 0;

	/*
	 * if (debug)
	 * printf("Database status: %s\n", mysql_stat(sql));
	 */

	if (mysql_ping(sql)) {
		fprintf(stderr, "acu_query: Connection is down and cannot reconnect\n");
		return 0;
	}

	if (mysql_query(sql, query_str)) {
		fprintf(stderr, "%s: %s\n", msg, mysql_error(sql));
		return 0;
	}

	return 1;
}


