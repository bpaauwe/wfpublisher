/*
 * WeatherFlow data publisher
 *
 * Copyright 2018 Bob Paauwe
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
 *
 * ----------------------------------------------------------------------------
 *  Description:
 *
 *  The WeatherFlow publisher listens for UDP packets from the WeatherFlow
 *  hub. It parses and consolidates those packets into a set of weather
 *  data that is then sent to various weather services like WeatherBug
 *  or CWOP.
 *
 *  Because the hub can send some types of data at a frequency greater
 *  than once a minute, in some cases, only the last recieved value is used.
 *  For example if 3 temperature readings are recevied in a 1 minute interval,
 *  the first two are ignored and the last is saved/sent.  This is not true
 *  for wind gust data.  The highest reported speed is always used.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <mysql/mysql.h>
#include "wfp.h"
#include "cJSON.h"

#define LOG "/home/pi/weather.log"

static bool connect_to_database(MYSQL *sql, char *db_host, char *db_name);
static int update_database(void);
static int wf_message_parse(char *msg);
static void wfp_air_parse(cJSON *air);
static void wfp_sky_parse(cJSON *sky);
static double TempF(double tempc);
static double MS2MPH(double ms);
static double calc_dewpoint(void);
static double calc_heatindex(void);
static double calc_windchill(void);
static double calc_feelslike(void);
static int calc_pressure_trend(void);

extern void send_to(int service, weather_data_t *wd);
extern void *start_server(void *args);
extern void json_data_copy(weather_data_t *wd);
extern void rainfall(double amount);
extern double get_rain_hourly(void);
extern double get_rain_daily(void);
extern double get_rain_monthly(void);
extern double get_rain_yearly(void);
extern double get_rain_1hr(void);
extern double get_rain_24hrs(void);
extern char *time_stamp(int gmt, int mode);

/* Globals */
MYSQL *sql = NULL;       /* Database handle */
int testmode = 0;        /* set when invoked in test mode */
int debug = 0;           /* set when debugging output is enabled */
int verbose = 0;         /* set when verbose output is enabled */
int dont_send = 0;       /* send data to weather services */
bool connected = false;
char *log_filename = LOG;
weather_data_t wd;

bool skip_wu = false;
bool skip_wb = false;
bool skip_cw = false;
bool skip_pws = false;
bool skip_log = false;
bool skip_db = true;

int main (int argc, char **argv)
{
	char line[1024];
	int i;
	int bytes;
	pthread_t api_thread;
	char *database = DB_NAME;
	char *host = DB_HOST;
	int sock;
	struct sockaddr_in s;
	int optval;

	/* process command line arguments */
	if (argc > 1) {
		for(i = 1; i < argc; i++) {
			if (argv[i][0] == '-') { /* An option */
				if (strcmp(&argv[i][1], "nowb") == 0) {
					skip_wb = true;
				} else if (strcmp(&argv[i][1], "nowu") == 0) {
					skip_wu = true;
				} else if (strcmp(&argv[i][1], "nocw") == 0) {
					skip_cw = true;
				} else if (strcmp(&argv[i][1], "nopws") == 0) {
					skip_pws = true;
				} else if (strcmp(&argv[i][1], "nolog") == 0) {
					skip_log = true;
				} else {
					switch (argv[i][1]) {
						case 't': /* testmode */
							testmode = 1;
							debug = 1; /* implies debug mode */
							break;
						case 'd': /* debug */
							debug = 1;
							break;
						case 'v': /* verbose */
							/* FIXME:
							 * *   This should be rolled into debug, might
							 * *   want to have different debug levels though
							 * */
							verbose = 1;
							if (strcmp(argv[i], "vv") == 0)
								verbose = 2;
							if (strcmp(argv[i], "vvv") == 0)
								verbose = 3;
							break;
						case 'l': /* log file */
							i++;
							log_filename = strdup(argv[i]);
							break;
						case 'o': /* database */
							i++;
							database = strdup(argv[i]);
							break;
						case 'n': /* database host */
							i++;
							host = strdup(argv[i]);
							break;
						case 's':
							dont_send = 1;
							break;
						default:
							printf("usage: %s [-t] [-d]\n", argv[0]);
							printf("        -t runs in test mode\n");
							printf("        -s don't send to weather services\n");
							printf("        -v verbose output\n");
							printf("        -d turns on debugging\n");
							printf("        -l <file> log to named file\n");
							printf("        -o <database> use database\n");
							printf("        -n <host> use database host\n");
							printf("        -f use tcpflow test string\n");
							printf("\n");

							exit(0);
							break;
					}
				}
			}
		}
	}

	memset(&wd, 0, sizeof(weather_data_t));


	if (!skip_db) {
		/* Initialize and open connection to database */
		if ((sql = mysql_init(NULL)) == NULL) {
			fprintf(stderr, "Failed to initialize MySQL interface.\n");
			exit(-1);
		}

		if (connect_to_database(sql, host, database)) {
			connected = true;
		}
	}

	/*
	 * Start a thread to publish the data. The thread will wake up
	 * and start new threads to send the data to each of the enabled
	 * services.
	 */

	/*
	 * The main loop will just wait for UDP packets on port
	 * 50222. Each packet is parsed and the data stored, overwriting
	 * any previous value.
	 */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	optval = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
		(const void *)&optval , sizeof(int));


	memset(&s, 0, sizeof(struct sockaddr_in));
	s.sin_family = AF_INET;
	s.sin_port = (in_port_t)htons(50222);
	s.sin_addr.s_addr = htonl(INADDR_ANY);

	bind(sock, (struct sockaddr *)&s, sizeof(s));

	while ((bytes = read(sock, line, 1024)) > 0) {
		line[bytes] = '\0';
		wf_message_parse(line);
		//printf("recv: %s\n", line);
	}

	if (!skip_db) mysql_close(sql);
	close(sock);

	exit(0);
}

static int wf_message_parse(char *msg) {
	cJSON *msg_json;
	const cJSON *type = NULL;
	int status = 0;

	msg_json = cJSON_Parse(msg);
	if (msg == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL) {
			fprintf(stderr, "Error before: %s\n", error_ptr);
		}
		status = 0;
		goto end;
	}

	type = cJSON_GetObjectItemCaseSensitive(msg_json, "type");
	if (cJSON_IsString(type) && (type->valuestring != NULL)) {
		if (strcmp(type->valuestring, "obs_air") == 0) {
			printf("-> Air packet\n");
			wfp_air_parse(msg_json);
		} else if (strcmp(type->valuestring, "obs_sky") == 0) {
			printf("-> Sky packet\n");
			wfp_sky_parse(msg_json);
		} else if (strcmp(type->valuestring, "rapid_wind") == 0) {
			printf("-> Rapid Wind packet\n");
		} else if (strcmp(type->valuestring, "evt_strike") == 0) {
			printf("-> Lightning strike packet\n");
		} else if (strcmp(type->valuestring, "evt_precip") == 0) {
			printf("-> Rain start packet\n");
		} else if (strcmp(type->valuestring, "device_status") == 0) {
			printf("-> Device status packet\n");
		} else if (strcmp(type->valuestring, "hub_status") == 0) {
			printf("-> Hub status packet\n");
		} else {
			printf("-> Unknown packet type: %s\n", type->valuestring);
		}

		//printf("%s\n", cJSON_Print(msg_json));
	}

end:
	cJSON_Delete(msg_json);
	return status;
}

#define SETWD(j, w, v) { \
	tmp = cJSON_GetArrayItem(j, v); \
	w = tmp->valuedouble; \
	}

#define SETWI(j, w, v) { \
	tmp = cJSON_GetArrayItem(j, v); \
	w = tmp->valueint; \
	}

static void wfp_air_parse(cJSON *air) {
	cJSON *obs;
	cJSON *ob;
	cJSON *tmp;
	int i;

	/* this is a 2 dimensional array [[v,v,v,v,v,v,v]] */
	obs = cJSON_GetObjectItemCaseSensitive(air, "obs");
	for (i = 0 ; i < cJSON_GetArraySize(obs) ; i++) {
		ob = cJSON_GetArrayItem(obs, i);
		printf("At index %d - Found array with %d items\n", i, cJSON_GetArraySize(ob));

		tmp = cJSON_GetArrayItem(ob, 0);
		printf("time = %d\n", tmp->valueint);

		SETWD(ob, wd.pressure, 1);		// millibars
		SETWD(ob, wd.temperature, 2)	// Celsius
		SETWD(ob, wd.humidity, 3)		// percent
		SETWI(ob, wd.strikes, 4)		// count
		SETWD(ob, wd.distance, 5)		// kilometers

		/* derrived values */
		wd.dewpoint = calc_dewpoint();	// farhenhi
		wd.heatindex = calc_heatindex();// Celsius
		wd.trend = calc_pressure_trend();
	}
}

static void wfp_sky_parse(cJSON *sky) {
	cJSON *obs;
	cJSON *ob;
	cJSON *tmp;
	int i;

	/* this is a 2 dimensional array [[v,v,v,v,v,v,v]] */
	obs = cJSON_GetObjectItemCaseSensitive(sky, "obs");
	for (i = 0 ; i < cJSON_GetArraySize(obs) ; i++) {
		ob = cJSON_GetArrayItem(obs, i);
		printf("At index %d - Found array with %d items\n", i, cJSON_GetArraySize(ob));

		tmp = cJSON_GetArrayItem(ob, 0);
		printf("time = %d\n", tmp->valueint);

		SETWD(ob, wd.illumination, 1);
		SETWI(ob, wd.uv, 2);
		SETWD(ob, wd.rain, 3);
		SETWD(ob, wd.windspeed, 5); // m/s
		SETWD(ob, wd.gustspeed, 6); // m/s
		SETWD(ob, wd.winddirection, 7);
		SETWD(ob, wd.solar, 10);
		SETWD(ob, wd.daily_rain, 11);

		/* derrived values */
		wd.windchill = calc_windchill();
	}
}

static double calc_dewpoint(void) {
	double b;
	double h;

	b = (17.625 * wd.temperature) / (243.04 + wd.temperature);
	h = log(h/100);

	return (243.04 * (h + b)) / (17.625 - h - b);

}

/*
 * Returns temp in F
 */
static double calc_heatindex(void) {
	double t = TempF(wd.temperature);
	double h = wd.humidity;
	double c1 = -42.379;
	double c2 = 2.04901523;
	double c3 = 10.14333127;
	double c4 = -0.22475541;
	double c5 = -6.83783 * pow(10, -3);
	double c6 = -5.481717 * pow(10, -2);
	double c7 = 1.22874 * pow(10, -3);
	double c8 = 8.5282 * pow(10, -4);
	double c9 = -1.99 * pow(10, -6);

	if ((t < 80.0) || (h < 40.0))
		return t;
	else
		return (c1 + (c2 * t) + (c3 * h) + (c4 * t * h) + (c5 * t * t) + (c6 * h * h) + (c7 * t * t * h) + (c8 * t * h * h) + (c9 * t * t * h * h));
}

/*
 * Returns temp in F
 */
static double calc_windchill(void) {
	double t = TempF(wd.temperature);
	double v = MS2MPH(wd.windspeed);

	if ((t < 50.0) && (v > 5.0))
		return 35.74 + (0.6215 * t) - (35.75 * pow(v, 0.16)) + (0.4275 * t * pow(v, 0.16));
	else
		return t;
}

/*
 * Returns temp in F
 */
static double calc_feelslike(void) {
	double wv;
	double ws;

	if (TempF(wd.temperature) >= 80)
		return calc_heatindex();
	else if (TempF(wd.temperature) < 50)
		return calc_windchill();
	else
		return TempF(wd.temperature);
}

static int calc_pressure_trend(void) {
	return 0.0;
}


static double TempF(double tempc) {
	return (tempc * 1.8) + 32;
}

static double MS2MPH(double ms) {
	return ms / 0.44704;
}


#if 0
static void set_dewpoint(void)
{
	double humidity = <tbd>;
	double T;
	double B;
	double dewpoint_c;

	if (humidity == 0.0)
		return;

	/* convert temperature to Celcius */
	T = (sensors[TEMPERATURE].data - 32) * 5 / 9;

	B = (log(humidity / 100) + ((17.27 * T) / (237.3 + T))) / 17.27;
	dewpoint_c = (237.3 * B) / (1 - B);

	sensors[s].data = (dewpoint_c * 9 / 5 + 32);
}

static void set_heatindex(enum SENSORS s)
{
	double T = sensors[TEMPERATURE].data;
	double R = sensors[HUMIDITY].data;
	double C1 = -42.379;
	double C2 = 2.04901523;
	double C3 = 10.14333127;
	double C4 = -0.22475541;
	double C5 = -6.83783 * pow(10,-3);
	double C6 = -5.481717 * pow(10,-2);
	double C7 = 1.22874 * pow(10,-3);
	double C8 = 8.5282 * pow(10,-4);
	double C9 = -1.99 * pow(10,-6);

	if ((T < 80.0) || (R < 40.0))
		sensors[s].data = T;
	else
		sensors[s].data = C1 + (C2 * T) + (C3 * R) + (C4 * T * R) +
			(C5 * T * T) + (C6 * R * R) + (C7 * T * T * R) + (C8 * T * R * R) +
			(C9 * T * T * R * R);
}

static void set_gusts(enum SENSORS s)
{
	if (sensors[WINDSPEED].data > sensors[GUSTSP].data) {
		sensors[GUSTSP].data  = sensors[WINDSPEED].data;
		sensors[GUSTDIR].data = sensors[WINDDIR].data;
	}
}
#endif

#if 0
static void set_rainfall(enum SENSORS s)
{
	switch (s) {
		case RAIN_HR:
			WD(s) = get_rain_hourly();
			break;
		case RAIN_DAY:
			WD(s) = get_rain_daily();
			break;
		case RAIN_MONTH:
			WD(s) = get_rain_monthly();
			break;
		case RAIN_YEAR:
			WD(s) = get_rain_yearly();
			break;
		case RAIN_LHR:
			WD(s) = get_rain_1hr();
			break;
		case RAIN_L24:
			WD(s) = get_rain_24hrs();
			break;
		default:
			//sensors[s].data += sensors[RAINFALL].data;
			break;
	}
}
#endif


static int my_getline(FILE *fp, char *s, int lim)
{
	int c, i = 0;
	int ws = 0;

	while(--lim > 0 && (c = fgetc(fp)) != EOF && c != '\n') {
		if(c != '\r') {
			if(!ws && ((c == ' ') || (c == '\t'))) {
				/* skip leading white space */
			} else {
				s[i++] = c;
				ws = 1;
			}
		}
	}

	s[i] = '\0';
	if( c == EOF ) {
		return (c);
	} else {
		return(i);
	}
}


/*
 * make a copy the weather data for the publishing functions
 */
static weather_data_t *weather_data(void)
{
	weather_data_t *wd;

	wd = calloc(1, sizeof(weather_data_t));

#if 0
	wd->pressure = WD(PRESSURE);
	wd->uncalibrated = WD(ABSOLUTE);
	wd->temperature = WD(TEMPERATURE);
	wd->humidity = WD(HUMIDITY);
	wd->windspeed = WD(WINDSPEED);
	wd->winddirection = WD(WINDDIR);
	wd->gustspeed = WD(GUSTSP);
	wd->gustdirection = WD(GUSTDIR);
	wd->rainfall_1min = WD(RAINFALL);
	wd->rainfall_1hr = WD(RAIN_HR);
	wd->rainfall_day = WD(RAIN_DAY);
	wd->rainfall_month = WD(RAIN_MONTH);
	wd->rainfall_year = WD(RAIN_YEAR);
	wd->dewpoint = WD(DEWPOINT);
	wd->heatindex = WD(HEATINDEX);
	//wd->timestamp = strdup(sensors[TIMESTAMP].name);
#endif

	return wd;
}


/*******
 *  Database Functions.
 *
 */


/*
 * Make a connection to the database server.
 */
static bool connect_to_database(MYSQL *sql, char *db_host, char *db_name)
{
	my_bool reconnect = 1;

	printf("Connecting to database %s:%s\n", db_host, db_name);
	mysql_options(sql, MYSQL_OPT_RECONNECT, &reconnect);

	if (!mysql_real_connect(sql, db_host, DB_USER, DB_PASS, NULL, 0, NULL, 0)) {
		fprintf(stderr, "Failed to connect to database: Error: %s\n",
				mysql_error(sql));
		return false;
	}


	if (mysql_select_db(sql, db_name)) {
		fprintf(stderr, "Error selecting database: Error: %s\n",
				mysql_error(sql));
		return false;
	}

	printf("database is now conencted\n");
	return true;
}

/*
 * Make a query to the database.  Keep trying until the query succeeds.
 *
 * Should this fail if the query fails?  Can the flow abort gracefully
 * if this fails?
 */
static bool acu_query(char *query_str, char *msg)
{
	if (!connected)
		return false;

	if (sql == NULL)
		return false;

	/*
	if (debug)
		printf("Database status: %s\n", mysql_stat(sql));
	*/

	if (mysql_ping(sql)) {
		fprintf(stderr, "acu_query: Connection is down and cannot reconnect\n");
		return false;
	}

	if (mysql_query(sql, query_str)) {
		fprintf(stderr, "%s: %s\n", msg, mysql_error(sql));
		return false;
	}

	return true;
}


/*
 * Update the database record with any new data.
 *
 * For debugging, may want to look at most recent record. to do that
 * select * from weather_log order by timestamp desc limit 1
 */
static int update_database(void)
{
	char *query;
	int ret = 0;

	/* Generate the sql statment to update the database. */
#if 0
	ret = asprintf(&query, "insert into weather_log set "
			"pressure=\"%f\","
			"uncalibrated=\"%f\","
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
			"heatindex=\"%f\","
			"garage_temperature=\"%f\","
			"garage_humidity=\"%f\","
			"temperature_2=\"%f\","
			"humidity_2=\"%f\","
			"valid=\"%lu\"",
			WD(PRESSURE), WD(ABSOLUTE),
			WD(TEMPERATURE), WD(HUMIDITY),
			WD(WINDSPEED), WD(WINDDIR),
			WD(GUSTSP), WD(GUSTDIR),
			WD(RAINFALL), WD(RAIN_HR), WD(RAIN_DAY),
			WD(RAIN_MONTH), WD(RAIN_YEAR),
			WD(DEWPOINT),
			WD(HEATINDEX),
			WD(TEMPERATURE2), WD(HUMIDITY2),
			WD(TEMPERATURE3), WD(HUMIDITY3),
#endif

	if (ret == -1)
		return ret;

	if (!testmode) {
		if (!acu_query(query, "Failed to update record"))
			ret = -1;

		if (debug) {
			printf("\nDB Update: %s\n", query);
		}
	} else {
		printf("\nTEST Updating database with: %s\n", query);
	}

	free(query);

	return ret;
}

int rainfall_data_save(double min, double hour, double day, double month, double year)
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

	if (!acu_query(query, "Failed to update rainfall data")) {
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
		if (!acu_query(query, "Failed to update rainfall data"))
			ret = -1;
	}

	free(query);
	return ret;
}

static char *rain_data_query = "select * from rainfall";
void rainfall_data_get(double *minute, double *hour, double *day,
		double *month, double *year)
{
	MYSQL_RES *result;
	MYSQL_ROW row;

	if (acu_query(rain_data_query, "Failed to access rainfall data")) {
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
