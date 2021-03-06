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
#include <errno.h>
#include "wfp.h"
#include "cJSON.h"

#define GUST_INTERVAL 30 /* Seconds for gust tracking */

static int wf_message_parse(char *msg);
static void wfp_air_parse(cJSON *air);
static void wfp_sky_parse(cJSON *sky);
static void wfp_wind_parse(cJSON *wind);
static void wfp_tower_parse(cJSON *tower);
static void read_config(void);
static void *publish(void *args);
static void initialize_publishers(void);
static void cleanup_publishers(void);
static void read_rainfall(void);
static void sinfo_free(struct service_info *info);

extern void send_to(struct service_info *sinfo, weather_data_t *wd);
extern void rainfall(double amount);
extern void accumulate_rain(weather_data_t *wd, double rain);
extern int mqtt_init(void);
extern void mqtt_disconnect(void);

/* Globals */
int debug = 0;           /* set when debugging output is enabled */
int verbose = 0;         /* set when verbose output is enabled */
weather_data_t wd;
int interval = 0;
struct service_info *sinfo = NULL;
struct station_info station;
cJSON *sensor_mapping = NULL;

static pthread_mutex_t data_event_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  data_event_trigger = PTHREAD_COND_INITIALIZER;

#define AIRDATA 0x01
#define SKYDATA 0x02

int main (int argc, char **argv)
{
	char line[1024];
	int i;
	int bytes;
	pthread_t send_thread;
	int sock;
	struct sockaddr_in s;
	int optval;
	int st = 0;
	time_t t = time(NULL);
	struct tm now;
	struct tm start;

	/* process command line arguments */
	if (argc > 1) {
		for(i = 1; i < argc; i++) {
			if (argv[i][0] == '-') { /* An option */
				switch (argv[i][1]) {
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
					default:
						printf("usage: %s [-d]\n", argv[0]);
						printf("        -v verbose output\n");
						printf("        -d turns on debugging\n");
						printf("\n");

						exit(0);
						break;
				}
			}
		}
	}

	localtime_r(&t, &start);
	memset(&wd, 0, sizeof(weather_data_t));
	wd.temperature_high = -100;
	wd.temperature_low = 150;

	read_config();
	read_rainfall();

	initialize_publishers();

	/*
	 * Start a thread to publish the data. The thread will wake up
	 * and start new threads to send the data to each of the enabled
	 * services.
	 */
	pthread_create(&send_thread, NULL, publish, NULL);

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
		t = time(NULL);
		localtime_r(&t, &now);
		if (now.tm_mday != start.tm_mday) {
			wd.temperature_high = -100;
			wd.temperature_low = 150;
			localtime_r(&t, &start);
		}

		line[bytes] = '\0';
		st |= wf_message_parse(line);
		//printf("recv: %s\n", line);

		/* If we have data to publish */
		if (st == (AIRDATA | SKYDATA)) {
			pthread_mutex_lock(&data_event_mutex);
			pthread_cond_signal(&data_event_trigger);
			pthread_mutex_unlock(&data_event_mutex);
			st = 0;
		}
	}

	close(sock);
	pthread_cancel(send_thread);
	cleanup_publishers();
	cJSON_Delete(sensor_mapping);
	sinfo_free(sinfo);
	free(station.name);
	free(station.location);
	free(station.latitude);
	free(station.longitude);
	free_trend();

	free(wd.timestamp);
	while (wd.tower_list) {
		struct sensor_list *l = wd.tower_list;
		wd.tower_list = wd.tower_list->next;

		free(l->sensor->sensor_id);
		free(l->sensor->timestamp);
		free(l->sensor);
		free(l);
	}


	exit(0);
}

static int wf_message_parse(char *msg) {
	cJSON *msg_json;
	const cJSON *type = NULL;
	int ret = 0;

	msg_json = cJSON_Parse(msg);
	if (msg == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL) {
			fprintf(stderr, "Error before: %s\n", error_ptr);
		}
		goto end;
	}

	type = cJSON_GetObjectItemCaseSensitive(msg_json, "type");
	if (cJSON_IsString(type) && (type->valuestring != NULL)) {
		if (strcmp(type->valuestring, "obs_air") == 0) {
			if (verbose) printf("-> Air packet\n");
			wfp_air_parse(msg_json);
			ret = AIRDATA;
		} else if (strcmp(type->valuestring, "obs_sky") == 0) {
			if (verbose) printf("-> Sky packet\n");
			wfp_sky_parse(msg_json);
			ret = SKYDATA;
		} else if (strcmp(type->valuestring, "rapid_wind") == 0) {
			if (verbose) printf("-> Rapid Wind packet\n");
			wfp_wind_parse(msg_json);
		} else if (strcmp(type->valuestring, "evt_strike") == 0) {
			if (verbose) printf("-> Lightning strike packet\n");
		} else if (strcmp(type->valuestring, "evt_precip") == 0) {
			if (verbose) printf("-> Rain start packet\n");
		} else if (strcmp(type->valuestring, "device_status") == 0) {
			if (verbose) printf("-> Device status packet\n");
		} else if (strcmp(type->valuestring, "hub_status") == 0) {
			if (verbose) printf("-> Hub status packet\n");
		} else if (strcmp(type->valuestring, "obs_tower") == 0) {
			if (verbose) printf("-> Tower packet\n");
			wfp_tower_parse(msg_json);
		} else {
			if (verbose) printf("-> Unknown packet type: %s\n", type->valuestring);
			printf("-> Unknown packet type: %s\n", type->valuestring);
		}

		//printf("%s\n", cJSON_Print(msg_json));
	}

end:
	cJSON_Delete(msg_json);
	return ret;
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
	struct tm *lt;

	if (debug) {
		tmp = cJSON_GetObjectItemCaseSensitive(air, "serial_number");
		printf("AIR data serial number: %s\n", tmp->valuestring);
	}

	/* this is a 2 dimensional array [[v,v,v,v,v,v,v]] */
	obs = cJSON_GetObjectItemCaseSensitive(air, "obs");
	for (i = 0 ; i < cJSON_GetArraySize(obs) ; i++) {
		ob = cJSON_GetArrayItem(obs, i);

		/* First item is a timestamp, lets use it for last update */
		tmp = cJSON_GetArrayItem(ob, 0);
		lt = localtime((long *)&tmp->valueint);
		if (wd.timestamp == NULL)
			wd.timestamp = (char *)malloc(25);
		sprintf(wd.timestamp, "%4d-%02d-%02d %02d:%02d:%02d",
				lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
				lt->tm_hour, lt->tm_min, lt->tm_sec);

		SETWD(ob, wd.pressure, 1);		// millibars
		SETWD(ob, wd.temperature, 2)	// Celsius
		SETWD(ob, wd.humidity, 3)		// percent
		SETWI(ob, wd.strikes, 4)		// count
		SETWD(ob, wd.distance, 5)		// kilometers

		/* derrived values */
		wd.pressure_sealevel = station_2_sealevel(wd.pressure,
				(station.elevation * .3048));
		wd.dewpoint = calc_dewpoint(wd.temperature, wd.humidity);	// farhenhi
		wd.heatindex = calc_heatindex(wd.temperature, wd.humidity);// Celsius
		wd.trend = calc_pressure_trend(wd.pressure);
		if (wd.temperature > wd.temperature_high)
			wd.temperature_high = wd.temperature;
		if (wd.temperature < wd.temperature_low)
			wd.temperature_low = wd.temperature;
	}
}

static void wfp_sky_parse(cJSON *sky) {
	cJSON *obs;
	cJSON *ob;
	cJSON *tmp;
	int i;

	if (debug) {
		tmp = cJSON_GetObjectItemCaseSensitive(sky, "serial_number");
		printf("SKY data serial number: %s\n", tmp->valuestring);
	}

	/* this is a 2 dimensional array [[v,v,v,v,v,v,v]] */
	obs = cJSON_GetObjectItemCaseSensitive(sky, "obs");
	for (i = 0 ; i < cJSON_GetArraySize(obs) ; i++) {
		ob = cJSON_GetArrayItem(obs, i);

		tmp = cJSON_GetArrayItem(ob, 0);

		SETWD(ob, wd.illumination, 1);
		SETWI(ob, wd.uv, 2);
		SETWD(ob, wd.rain, 3);     // over reporting interval
		SETWD(ob, wd.windspeed, 5); // m/s
		SETWD(ob, wd.winddirection, 7);
		SETWD(ob, wd.solar, 10);


		/* derrived values */
		strncpy(wd.wind_dir, DegreesToCardinal(wd.winddirection), 3);
		wd.windchill = calc_windchill(wd.temperature, wd.windspeed);
		wd.feelslike = calc_feelslike(wd.temperature, wd.windspeed, wd.humidity);

		/* Track maximum gust over 10 intervals */
		if (interval == GUST_INTERVAL) {
			SETWD(ob, wd.gustspeed, 6); // m/s
			wd.gustdirection = wd.winddirection;
			interval = 0;
		} else {
			tmp = cJSON_GetArrayItem(ob, 6);
			if (tmp->valuedouble > wd.gustspeed) {
				wd.gustspeed = tmp->valuedouble;
				wd.gustdirection = wd.winddirection;
			}
			interval++;
		}

		/* Track rainfall over time */
		accumulate_rain(&wd, wd.rain);

	}
}

/*
 * parse the rapid wind messages.  Use these to
 * update the gust information.
 */
static void wfp_wind_parse(cJSON *wind) {
	cJSON *obs;
	cJSON *ob;
	int direction;

	/* this is a 1 dimensional array [v,v,v,v,v,v,v] */
	/* ob":[1493322445,2.3,128] */
	obs = cJSON_GetObjectItemCaseSensitive(wind, "ob");

	ob = cJSON_GetArrayItem(obs, 2); /* wind direction */
	direction = ob->valueint;

	ob = cJSON_GetArrayItem(obs, 1); /* wind speed */
	if (interval == GUST_INTERVAL) {
		wd.gustspeed = ob->valuedouble;
		wd.gustdirection = direction;
		interval = 0;
	} else {
		if (ob->valuedouble > wd.gustspeed) {
			wd.gustspeed = ob->valuedouble;
			wd.gustdirection = direction;
		}
	}
}

static void wfp_tower_parse(cJSON *tower) {
	cJSON *obs;
	cJSON *ob;
	cJSON *tmp;
	cJSON *cfg;
	int i;
	struct tm *lt;
	struct sensor_list *list;

	tmp = cJSON_GetObjectItemCaseSensitive(tower, "serial_number");
	if (debug)
		printf("Tower data serial number: %s\n", tmp->valuestring);

	list = wd.tower_list;
	while(list) {
		if (strcmp(list->sensor->sensor_id, tmp->valuestring) == 0)
			break;
		list = list->next;
	}

	if (!list) {
		list = (struct sensor_list *)malloc(sizeof(struct sensor_list));
		if (!list) {
			printf("Failed to allocate memory for sensor.\n");
			return;
		}
		list->sensor = (struct sensor_data *)malloc(sizeof(struct sensor_data));
		memset (list->sensor, 0, sizeof(struct sensor_data));
		list->sensor->sensor_id = strdup(tmp->valuestring);
		list->sensor->temperature_high = -100;
		list->sensor->temperature_low = 100;
		list->next = NULL;

		/* Lookup the location */
		for (i = 0 ; i < cJSON_GetArraySize(sensor_mapping) ; i++) {
			cfg = cJSON_GetArrayItem(sensor_mapping, i);
			tmp = cJSON_GetObjectItemCaseSensitive(cfg, "serial_number");
			if (strcmp(list->sensor->sensor_id, tmp->valuestring) == 0) {
				tmp = cJSON_GetObjectItemCaseSensitive(cfg, "location");
				strcpy(list->sensor->location, tmp->valuestring);
				break;
			}
		}

		if (list->sensor->location[0] == '\0')
			strcpy(list->sensor->location, list->sensor->sensor_id);

		/* Add to head of list */
		list->next = wd.tower_list;
		wd.tower_list = list;
	}

	obs = cJSON_GetObjectItemCaseSensitive(tower, "obs");
	for (i = 0 ; i < cJSON_GetArraySize(obs) ; i++) {
		ob = cJSON_GetArrayItem(obs, i);

		/* First item is a timestamp, lets use it for last update */
		tmp = cJSON_GetArrayItem(ob, 0);
		lt = localtime((long *)&tmp->valueint);
		if (list->sensor->timestamp == NULL)
			list->sensor->timestamp = (char *)malloc(25);
		sprintf(list->sensor->timestamp, "%4d-%02d-%02d %02d:%02d:%02d",
				lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
				lt->tm_hour, lt->tm_min, lt->tm_sec);

		SETWD(ob, list->sensor->temperature, 2)	// Celsius
		SETWD(ob, list->sensor->humidity, 3)		// percent

		if (list->sensor->temperature > list->sensor->temperature_high)
			list->sensor->temperature_high = list->sensor->temperature;
		if (list->sensor->temperature < list->sensor->temperature_low)
			list->sensor->temperature_low = list->sensor->temperature;
	}
}


/*
 * Given a publishing service, fill in the function pointer
 * table for the init, update, cleanup functions.
 *
 * This will need to be updated for every new service.
 */
static void service_setup(struct service_info *s)
{
	if (strcmp(s->service, "logfile") == 0)
		log_setup(s);
	else if (strcmp(s->service, "WeatherUnderground") == 0)
		wunderground_setup(s);
	else if (strcmp(s->service, "WeatherBug") == 0)
		wbug_setup(s);
	else if (strcmp(s->service, "PersonalWeatherStation") == 0)
		pws_setup(s);
	else if (strcmp(s->service, "CWOP") == 0)
		cwop_setup(s);
	else if (strcmp(s->service, "MQTT") == 0)
		mqtt_setup(s);
	else if (strcmp(s->service, "mysql") == 0)
		mysql_setup(s);
	else if (strcmp(s->service, "Display") == 0)
		display_setup(s);
	else
		printf("Unknown publishing service %s\n", s->service);
}

static void read_config(void) {
	FILE *fp;
	char *json;
	int len;
	cJSON *cfg_json;
	cJSON *services;
	cJSON *cfg;
	cJSON *mapping;
	const cJSON *type = NULL;
	int i;
	struct service_info *s;


	printf("Reading configuration file.\n");
	json = malloc(4096);
	fp = fopen("config", "r");
	len = fread(json, 1, 4096, fp);
	fclose (fp);

	json[len] = '\0';

	if (len > 0) {
		cfg_json = cJSON_Parse(json);
		services = cJSON_GetObjectItemCaseSensitive(cfg_json, "version");
		printf("Version = %s\n", services->valuestring);

		if ((type = cJSON_GetObjectItemCaseSensitive(cfg_json, "name")))
			station.name = strdup(type->valuestring);
		if ((type = cJSON_GetObjectItemCaseSensitive(cfg_json, "location")))
			station.location = strdup(type->valuestring);
		if ((type = cJSON_GetObjectItemCaseSensitive(cfg_json, "latitude")))
			station.latitude = strdup(type->valuestring);
		if ((type = cJSON_GetObjectItemCaseSensitive(cfg_json, "longitude")))
			station.longitude = strdup(type->valuestring);
		if ((type = cJSON_GetObjectItemCaseSensitive(cfg_json, "elevation")))
			station.elevation = type->valueint;

		services = cJSON_GetObjectItemCaseSensitive(cfg_json, "services");
		for (i = 0 ; i < cJSON_GetArraySize(services) ; i++) {
			cfg = cJSON_GetArrayItem(services, i);

			s = malloc(sizeof(struct service_info));
			memset(s, 0, sizeof(struct service_info));
			s->cfg.metric = 0;

			if ((type = cJSON_GetObjectItemCaseSensitive(cfg, "service")))
				s->service = strdup(type->valuestring);

			if ((type = cJSON_GetObjectItemCaseSensitive(cfg, "host")))
				s->cfg.host = strdup(type->valuestring);

			if ((type = cJSON_GetObjectItemCaseSensitive(cfg, "name")))
				s->cfg.name = strdup(type->valuestring);

			if ((type = cJSON_GetObjectItemCaseSensitive(cfg, "password")))
				s->cfg.pass = strdup(type->valuestring);

			if ((type = cJSON_GetObjectItemCaseSensitive(cfg, "extra")))
				s->cfg.extra = strdup(type->valuestring);

			if ((type = cJSON_GetObjectItemCaseSensitive(cfg, "metric")))
				s->cfg.metric = type->valueint;

			if ((type = cJSON_GetObjectItemCaseSensitive(cfg, "enabled")))
				s->enabled = type->valueint;

			if (station.name)
				s->station.name = strdup(station.name);
			if (station.location)
				s->station.location = strdup(station.location);
			if (station.latitude)
				s->station.latitude = strdup(station.latitude);
			if (station.longitude)
				s->station.longitude = strdup(station.longitude);
			s->station.elevation = station.elevation;

			printf("Found  %s (%s) %s\n", s->service, s->cfg.host,
					(s->enabled) ? "enabled" : "disabled");

			/*
			 * Is there some way to hook up s-funcs dynamically here?
			 * I don't want to have a static lookup that needs to be
			 * modified to match each publisher.
			 *
			 * Making each publisher a dynamic library and loading
			 * them at runtime works, but that seems like overkill as
			 * we then need to have a bunch of .so files sitting around
			 * with the executable for it to functon properly.
			 */
			service_setup(s);

			s->next = sinfo;
			sinfo = s;
		}

		mapping = cJSON_GetObjectItemCaseSensitive(cfg_json, "mapping");
		sensor_mapping = cJSON_Duplicate(mapping, 1);
		/*
		for (i = 0 ; i < cJSON_GetArraySize(mapping) ; i++) {
			cfg = cJSON_GetArrayItem(mapping, i);
			if ((type = cJSON_GetObjectItemCaseSensitive(cfg, "serial_number")))
				tower_map[i].serial_number = strdup(type->valuestring);
			if ((type = cJSON_GetObjectItemCaseSensitive(cfg, "location")))
				tower_map[i].location = strdup(type->valuestring);
		}
		*/
	}
	cJSON_Delete(cfg_json);
	free(json);
}


static void sinfo_free(struct service_info *info)
{
	struct service_info *list = info;

	while (info) {
		list = info;
		info = info->next;
		free(list->service);
		free(list->cfg.host);
		free(list->cfg.name);
		free(list->cfg.pass);
		free(list->cfg.extra);
		free(list->station.name);
		free(list->station.location);
		free(list->station.latitude);
		free(list->station.longitude);
		free(list);
	}
}

/*
 * Read the saved rainfall data and update the data structure
 */
static void read_rainfall(void) {
	FILE *fp;
	char *json;
	int len;
	cJSON *rain_json;
	cJSON *saved_at;
	cJSON *tmp;
	time_t t = time(NULL);
	struct tm gt;

	localtime_r(&t, &gt);

	printf("Reading rainfall file.\n");
	json = malloc(4096);
	fp = fopen("rainfall.json", "r");
	len = fread(json, 1, 4096, fp);
	fclose (fp);

	json[len] = '\0';

	if (len > 0) {
		rain_json = cJSON_Parse(json);
		saved_at = cJSON_GetObjectItemCaseSensitive(rain_json, "time");
		tmp = cJSON_GetObjectItemCaseSensitive(saved_at, "year");
		if (tmp) {
			if (tmp->valueint != gt.tm_year + 1900) {
				/* Year doesn't match so skip everything */
				fprintf(stderr, "Skipping rain, year doesn't match\n");
				free(json);
				return;
			}
		}

		/* Set saved yearly value */
		tmp = cJSON_GetObjectItemCaseSensitive(rain_json, "rain_current_year");
		wd.rainfall_year = tmp->valuedouble;

		tmp = cJSON_GetObjectItemCaseSensitive(saved_at, "month");
		if (tmp) {
			if (tmp->valueint == gt.tm_mon + 1) {
				tmp = cJSON_GetObjectItemCaseSensitive(rain_json,
						"rain_current_month");
				wd.rainfall_month = tmp->valuedouble;
			} else {
				/* month doesn't match, skip everything else */
				fprintf(stderr, "Skipping month rain.\n");
				free(json);
				return;
			}
		}

		tmp = cJSON_GetObjectItemCaseSensitive(saved_at, "day");
		if (tmp) {
			if (tmp->valueint == gt.tm_mday) {
				tmp = cJSON_GetObjectItemCaseSensitive(rain_json,
						"rain_current_day");
				wd.rainfall_day = tmp->valuedouble;
				tmp = cJSON_GetObjectItemCaseSensitive(rain_json, "rain_24");
				wd.rainfall_24hr = tmp->valuedouble;
			} else {
				fprintf(stderr, "Skipping day rain.\n");
				free(json);
				return;
			}
		}

		tmp = cJSON_GetObjectItemCaseSensitive(saved_at, "hour");
		if (tmp) {
			if (tmp->valueint == gt.tm_hour) {
				tmp = cJSON_GetObjectItemCaseSensitive(rain_json,
						"rain_current_hour");
				wd.rainfall_1hr = tmp->valuedouble;
				tmp = cJSON_GetObjectItemCaseSensitive(rain_json, "rain_60");
				wd.rainfall_60min = tmp->valuedouble;
			}
		}

	}
	cJSON_Delete(rain_json);
	free(json);
}


/*
 * initialize_publishers
 *
 * Call the publisher's initialization function
 */
static void initialize_publishers(void)
{
	struct service_info *sitr;

	for (sitr = sinfo; sitr != NULL; sitr = sitr->next) {
		if (sitr->funcs.init)
			(sitr->funcs.init)(&sitr->cfg, debug);
	}
}

static void cleanup_publishers(void)
{
	struct service_info *sitr;

	for (sitr = sinfo; sitr != NULL; sitr = sitr->next) {
		if (sitr->funcs.cleanup)
			(sitr->funcs.cleanup)();
	}
}

/*
 * Publish the weather data to the various services
 *
 * This function is invoked as a separate pthread. It waits for an
 * event from the main thread that indicates that new data is available.
 * When new data is ready, it loops through the configured publishing
 * services and sends the data to those that are enabled.
 */
static void *publish(void *args)
{
	struct service_info *sitr;

	while (1) {
		int res_wait;

		/* Wait for an event saying we've got new data */
		if (debug) fprintf(stderr, "Waiting on data available event\n");
		res_wait = pthread_cond_wait(&data_event_trigger, &data_event_mutex);
		if (res_wait == EINVAL) {
			fprintf(stderr, "Error waiting on event\n");
			continue;
		}
		if (debug) fprintf(stderr, "Data available event happened\n");

		/* Send the data to each enabled service */
		for (sitr = sinfo; sitr != NULL; sitr = sitr->next) {
			if (verbose)
				printf("%s is %s\n", sitr->service,
						(sitr->enabled ? "enabled" : "disabled"));
			if (sitr->enabled) {
				if (debug)
					printf("Sending weather data to service %s\n",
							sitr->cfg.host);
				send_to(sitr, &wd);
			}
		}
	}

	return 0;
}




