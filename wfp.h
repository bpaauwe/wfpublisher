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

#ifndef _WFP_H_
#define _WFP_H_

/*
 * Configuration information below.  It is built into the code rather than
 * read from a configuration file by design.
 */

struct service_info {
	char *host;
	char *name;
	char *pass;
	char *extra;
	char *location_lat;
	char *location_long;
	int enabled;
	int index;
};

#define LOCAL         0
#define WUNDERGROUND  1
#define WEATHERBUG    2
#define CWOP          3
#define PWS           4
#define DB_MYSQL      5
#define MQTT          6
#define SERVICE_END   7
#define for_each_service(s) for((s) = LOCAL; (s) < SERVICE_END; (s)++)


/*
 * This structure holds a data record. It is built from the current
 * database record, calculated values, and the data collected from the bridge.
 */
typedef struct _wd {
	char *timestamp;
	double pressure;
	double temperature;
	double humidity;
	double windspeed;
	double winddirection;
	double gustspeed;
	double gustdirection;
	double illumination;
	double distance;
	double solar;
	double uv;
	int strikes;
	double rain;
	double daily_rain;
	double rainfall_1hr;
	double rainfall_day;
	double rainfall_month;
	double rainfall_year;
	double rainfall_60min;
	double rainfall_24hr;
	double temperature_high;
	double temperature_low;
	double dewpoint;
	double heatindex;
	double windchill;
	double trend;
	double feelslike;
	unsigned long valid;
} weather_data_t;

/*
 * Provide a name for the database fields. This is used to access the
 * row array returned by the database query.  Makes the code a bit easier
 * to read.
 */
enum db_fields {
	timestamp = 0,
	pressure,
	temperature,
	humidity,
	windspeed,
	winddirection,
	gustspeed,
	gustdirection,
	rainfall_1min,
	rainfall_1hr,
	rainfall_day,
	dewpoint,
	heatindex,
	uncalibrated,
	garage_temperature,
	garage_humidity,
	temperature_2,
	humidity_2,
	valid,
	rainfall_month,
	rainfall_year
};

/* wfp-utils.c */
extern double calc_heatindex(double, double);
extern double calc_dewpoint(double, double);
extern double calc_windchill(double, double);
extern double mb2in(double mb);
extern double MS2MPH(double ms);
extern double TempF(double tempc);
extern int calc_pressure_trend(void);
extern double calc_feelslike(double, double, double);
extern char *time_stamp(int gmt, int mode);


#endif
