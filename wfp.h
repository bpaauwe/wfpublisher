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

struct sensor_data {
	char *sensor_id;
	char *timestamp;
	double temperature;
	double humidity;
	double temperature_high;
	double temperature_low;
	char location[50];
};

struct sensor_list {
	struct sensor_data *sensor;
	struct sensor_list *prev;
	struct sensor_list *next;
};


/*
 * This structure holds a data record. It is built from the current
 * database record, calculated values, and the data collected from the bridge.
 */
typedef struct _wd {
	char *timestamp;
	double pressure;
	double pressure_sealevel;
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
	char wind_dir[4];
	struct sensor_list *tower_list;
} weather_data_t;


struct mapping_info {
	char *serial_number;
	char *location;
};

/*
 * Configuration information below.  It is built into the code rather than
 * read from a configuration file by design.
 */


struct cfg_info {
	char *host;
	char *name;
	char *pass;
	char *extra;
	int metric;
};

struct station_info {
	char *name;
	char *location;
	char *latitude;
	char *longitude;
	int elevation;
};

struct publisher_funcs {
	int (*init)(struct cfg_info *info, int debug);
	void (*update)(struct cfg_info *info, struct station_info *station,
					weather_data_t *data);
	void (*cleanup)(void);
};

struct service_info {
	char *service;
	int enabled;
	int index;
	struct station_info station;
	struct cfg_info cfg;
	struct service_info *next;
	struct publisher_funcs funcs;
};


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

/* service setup */
extern void log_setup(struct service_info *s);
extern void wunderground_setup(struct service_info *s);
extern void wbug_setup(struct service_info *s);
extern void pws_setup(struct service_info *s);
extern void cwop_setup(struct service_info *s);
extern void mqtt_setup(struct service_info *s);
extern void mysql_setup(struct service_info *s);
extern void display_setup(struct service_info *s);

/* wfp-utils.c */
extern double calc_heatindex(double, double);
extern double calc_dewpoint(double, double);
extern double calc_windchill(double, double);
extern double mb2in(double mb);
extern double MS2MPH(double ms);
extern double TempF(double tempc);
extern char *DegreesToCardinal(double deg);
extern double station_2_sealevel(double, double);
extern int calc_pressure_trend(double);
extern void free_trend(void);
extern double calc_feelslike(double, double, double);
extern char *time_stamp(int gmt, int mode);
#define CONVERT_ALL 0x00
#define NO_PRESSURE 0x01
extern void unit_convert(weather_data_t *wd, unsigned int skip);


#endif
