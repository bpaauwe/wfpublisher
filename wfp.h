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


/* This defines the database name, account, and password */
#define DB_HOST "sirius.bobsplace.com"
#define DB_NAME "weather"
#define DB_USER "weather"
#define DB_PASS "weather"

/* This defines the connection information for Weather Underground */
#define WU_HOST       "weatherstation.wunderground.com"
#define WU_STATION_ID "KCAELDOR15"
#define WU_PASSWORD   "gesserit"


/* This defines the connection information for WeatherBug */
#define WB_HOST    "data.backyard2.weatherbug.com"
#define WB_ID      "p17648"
#define WB_KEY     "gesserit"
#define WB_NUM     "29244"

/* This defines the connection information for CWOP */
#define CWOP_HOST  "cwop.aprs.net"
#define CWOP_ID    "EW4740"
#define CWOP_LAT   "3840.67N"
#define CWOP_LONG  "12100.70W"

/* This defines the connection information for PWS Weather */
#define PWS_HOST    "www.pwsweather.com"
#define PWS_ID      "BOBSPLACE"
#define PWS_KEY     "gesserit"

#define LOCAL         0
#define WUNDERGROUND  1
#define WEATHERBUG    2
#define CWOP          3
#define PWS           4
#define DB_MYSQL      5
#define SERVICE_END   6
#define for_each_service(s) for((s) = LOCAL; (s) < SERVICE_END; (s)++)


enum SENSORS {
	TEMPERATURE = 0,
	TEMPERATURE2,
	TEMPERATURE3,
	HUMIDITY,
	HUMIDITY2,
	HUMIDITY3,
	WINDSPEED,
	WINDDIR,
	RAINFALL,
	GUSTSP,
	GUSTDIR,
	RAIN_HR,
	RAIN_DAY,
	RAIN_MONTH,
	RAIN_YEAR,
	RAIN_LHR,
	RAIN_L24,
	DEWPOINT,
	HEATINDEX,
	PRESSURE,
	ABSOLUTE,
	TIMESTAMP,
	VALID,
	END
};

#define for_each_sensor(p) for((p) = TEMPERATURE; (p) < END; (p)++)

/*
 * TODO:
 *   This could replace the weather_data_t structure below. Then
 *   the data collected becomes a lot more extensible.  You just
 *   need to add a new item to the array with some definition
 *   about how it gets populated.
 *
 *   if name is not NULL, then the data comes from the input stream.
 *   if the calculate function is not NULL, it's calculated data.
 */
typedef struct _sensor_info {
	int id;
	char *name;
	char *description;
	double data;  /* Future rework, change sensors to data fields */
	double (*parse)(char *);
	void (*calculate)(enum SENSORS s);
	void (*clear)(enum SENSORS s);
	char raw[12]; /* raw data from bridge */
} sensor_info_t;

typedef struct _raw {
	char sensor[12];
	char temperature[12];
	char humidity[12];
	char windspeed[12];
	char winddirection[12];
	char rainfall[12];
} raw_data_t;

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
	double rainfall_1min;
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
	raw_data_t raw;
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


#endif
