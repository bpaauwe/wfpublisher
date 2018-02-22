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

static int debug;

static int db_query(MYSQL *sql, char *query_str, char *msg);
static int connect_to_database(MYSQL *sql, char *db_host, char *db_name,
		char *db_user, char *db_pass);

/*
 * Store data in a MYSQL (or compatible) database
 */
void send_to_db(struct cfg_info *cfg, struct station_info *station,
				weather_data_t *wd)
{
	struct timeval start, end;
	char *ts_start, *ts_end;
	char *query;
	int ret;
	MYSQL *sql;

	gettimeofday(&start, NULL);

	if (!cfg->metric)
		unit_convert(wd, CONVERT_ALL);

	if (debug) {
		ts_start = time_stamp(0, 1);
		fprintf(stderr, "%s: Begin database update to %s\n", ts_start, cfg->host);
		free(ts_start);
	}

	/* Initialize and open connection to database */
	if ((sql = mysql_init(NULL)) == NULL) {
		fprintf(stderr, "Failed to initialize MySQL interface.\n");
		goto end;
	}

	ret = connect_to_database(sql, cfg->host, cfg->extra, cfg->name, cfg->pass);
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
			wd->pressure,
			wd->temperature,
			wd->humidity,
			wd->windspeed,
			wd->winddirection,
			wd->gustspeed,
			wd->rain,
			wd->rainfall_1hr,
			wd->rainfall_day,
			wd->rainfall_month,
			wd->rainfall_year,
			wd->winddirection,
			wd->dewpoint,
			wd->heatindex);

	db_query(sql, query, "Failed to update record");
	free(query);

end:
	mysql_close(sql);

	gettimeofday(&end, NULL);
	if (debug) {
		long diff;

		diff = ((end.tv_sec-start.tv_sec)*1000000 +
				(end.tv_usec-start.tv_usec)) / 1000;
		ts_end = time_stamp(0, 1);

		fprintf(stderr, "%s: Database update complete in %ld msecs\n",
				ts_end, diff);
		free(ts_end);
	}

	pthread_exit(NULL);
	return;
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


/*
 * Rainfall tracking
 *
 * Store/retrieve rainfall data using the database.
 */

int rainfall_data_save(MYSQL *sql, double min, double hour, double day,
		double month, double year)
{
	char *query;
	int ret = 0;

	/*
	 * If a database table has only 1 row, do we need to specify it
	 * in an update?
	 */
	/* Generate the sql statment to update the database. */
	ret = asprintf(&query, "update rainfall set "
			"minute_total=\"%f\","
			"hour_total=\"%f\","
			"day_total=\"%f\","
			"month_total=\"%f\","
			"year_total=\"%f\" "
			"where valid=\"Y\"",
			min, hour, day, month, year);
	if (ret == -1)
		return ret;

	if (!db_query(sql, query, "Failed to update rainfall data")) {
		/* Does this mean the record didn't exist? */
		free(query);
		ret = asprintf(&query, "insert into rainfall set "
				"minute_total=\"%f\","
				"hour_total=\"%f\","
				"day_total=\"%f\","
				"month_total=\"%f\","
				"year_total=\"%f\","
				"valid=\"Y\"",
				min, hour, day, month, year);
		if (!db_query(sql, query, "Failed to update rainfall data"))
			ret = -1;
	}

	free(query);
	return ret;
}

static char *rain_data_query = "select * from rainfall";
void rainfall_data_get(MYSQL *sql, double *minute, double *hour, double *day,
		double *month, double *year)
{
	MYSQL_RES *result;
	MYSQL_ROW row;

	if (db_query(sql, rain_data_query, "Failed to access rainfall data")) {
		/* Do row selection */
		result = mysql_use_result(sql);
		row = mysql_fetch_row(result);
		printf("queried rainfall data.  %s  %s  %s\n", (char *)row[2], (char *)row[3], (char *)row[4]);

		if (row[0]) {
			*minute = atof((char *)row[0]);
			*hour   = atof((char *)row[1]);
			*day    = atof((char *)row[2]);
			*month  = atof((char *)row[3]);
			*year   = atof((char *)row[4]);
		}
		mysql_free_result(result);
	}
}

static int db_init(struct cfg_info *cfg, int d)
{
	debug = d;
	return 0;
}

static const struct publisher_funcs mysql_funcs = {
	.init = db_init,
	.update = send_to_db,
	.cleanup = NULL
};

void mysql_setup(struct service_info *sinfo)
{
	sinfo->funcs = mysql_funcs;
	return;
}

