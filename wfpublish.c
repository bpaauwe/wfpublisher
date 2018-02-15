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

static int wf_message_parse(char *msg);
static void wfp_air_parse(cJSON *air);
static void wfp_sky_parse(cJSON *sky);
static void read_config(void);
static void *publish(void *args);
static void initialize_publishers(void);
static void cleanup_publishers(void);

extern void send_to(struct service_info *sinfo, weather_data_t *wd);
extern void rainfall(double amount);
extern void accumulate_rain(weather_data_t *wd, double rain);
extern int mqtt_init(void);
extern void mqtt_disconnect(void);

/* Globals */
MYSQL *sql = NULL;       /* Database handle */
int testmode = 0;        /* set when invoked in test mode */
int debug = 0;           /* set when debugging output is enabled */
int verbose = 0;         /* set when verbose output is enabled */
int dont_send = 0;       /* send data to weather services */
weather_data_t wd;
int interval = 0;
int status = 0;
struct service_info *sinfo = NULL;

int main (int argc, char **argv)
{
	char line[1024];
	int i;
	int bytes;
	pthread_t send_thread;
	int sock;
	struct sockaddr_in s;
	int optval;

	/* process command line arguments */
	if (argc > 1) {
		for(i = 1; i < argc; i++) {
			if (argv[i][0] == '-') { /* An option */
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
					case 's':
						dont_send = 1;
						break;
					default:
						printf("usage: %s [-t] [-d]\n", argv[0]);
						printf("        -t runs in test mode\n");
						printf("        -s don't send to weather services\n");
						printf("        -v verbose output\n");
						printf("        -d turns on debugging\n");
						printf("\n");

						exit(0);
						break;
				}
			}
		}
	}

	memset(&wd, 0, sizeof(weather_data_t));

	read_config();

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
		line[bytes] = '\0';
		wf_message_parse(line);
		//printf("recv: %s\n", line);
	}

	close(sock);

	cleanup_publishers();

	exit(0);
}

static int wf_message_parse(char *msg) {
	cJSON *msg_json;
	const cJSON *type = NULL;

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
			status |= 0x01;
		} else if (strcmp(type->valuestring, "obs_sky") == 0) {
			printf("-> Sky packet\n");
			wfp_sky_parse(msg_json);
			status |= 0x02;
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
		wd.dewpoint = calc_dewpoint(wd.temperature, wd.humidity);	// farhenhi
		wd.heatindex = calc_heatindex(wd.temperature, wd.humidity);// Celsius
		wd.trend = calc_pressure_trend();
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

	/* this is a 2 dimensional array [[v,v,v,v,v,v,v]] */
	obs = cJSON_GetObjectItemCaseSensitive(sky, "obs");
	for (i = 0 ; i < cJSON_GetArraySize(obs) ; i++) {
		ob = cJSON_GetArrayItem(obs, i);
		printf("At index %d - Found array with %d items\n", i, cJSON_GetArraySize(ob));

		tmp = cJSON_GetArrayItem(ob, 0);
		printf("time = %d\n", tmp->valueint);

		SETWD(ob, wd.illumination, 1);
		SETWI(ob, wd.uv, 2);
		SETWD(ob, wd.rain, 3);     // over reporting interval
		SETWD(ob, wd.windspeed, 5); // m/s
		SETWD(ob, wd.winddirection, 7);
		SETWD(ob, wd.solar, 10);

		/* derrived values */
		wd.windchill = calc_windchill(wd.temperature, wd.windspeed);
		wd.feelslike = calc_feelslike(wd.temperature, wd.windspeed, wd.humidity);

		/* Track maximum gust over 10 intervals */
		if (interval == 10) {
			SETWD(ob, wd.gustspeed, 6); // m/s
			interval = 0;
		} else {
			tmp = cJSON_GetArrayItem(ob, 6);
			if (tmp->valuedouble > wd.gustspeed)
				wd.gustspeed = tmp->valuedouble;
		}

		/* Track rainfall over time */
		accumulate_rain(&wd, wd.rain);

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

		services = cJSON_GetObjectItemCaseSensitive(cfg_json, "services");
		for (i = 0 ; i < cJSON_GetArraySize(services) ; i++) {
			cfg = cJSON_GetArrayItem(services, i);

			s = malloc(sizeof(struct service_info));

			type = cJSON_GetObjectItemCaseSensitive(cfg, "service");
			s->service = strdup(type->valuestring);

			type = cJSON_GetObjectItemCaseSensitive(cfg, "index");
			s->index = type->valueint;

			type = cJSON_GetObjectItemCaseSensitive(cfg, "host");
			s->cfg.host = strdup(type->valuestring);
			printf("At index %d - Found  %s", i, type->valuestring);

			type = cJSON_GetObjectItemCaseSensitive(cfg, "name");
			s->cfg.name = strdup(type->valuestring);

			type = cJSON_GetObjectItemCaseSensitive(cfg, "password");
			s->cfg.pass = strdup(type->valuestring);

			type = cJSON_GetObjectItemCaseSensitive(cfg, "extra");
			s->cfg.extra = strdup(type->valuestring);

			type = cJSON_GetObjectItemCaseSensitive(cfg, "location_lat");
			s->cfg.location_lat = strdup(type->valuestring);

			type = cJSON_GetObjectItemCaseSensitive(cfg, "location_long");
			s->cfg.location_long = strdup(type->valuestring);

			type = cJSON_GetObjectItemCaseSensitive(cfg, "enabled");
			s->enabled = type->valueint;
			printf("  enabled[%d]=%d\n", s->index, s->enabled);

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
	}
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
 */
static void *publish(void *args)
{
	struct service_info *sitr;

	/* Wait until we've actually received some data */
	while (status != (0x01 | 0x02)) {
		printf("Waiting for valid data to be received status = 0x%x\n", status);
		sleep(5);
	}

	while (1) {
		for (sitr = sinfo; sitr != NULL; sitr = sitr->next) {
			printf("%s is %s\n", sitr->cfg.host,
					(sitr->enabled ? "enabled" : "disabled"));
			if (sitr->enabled) {
				if (debug)
					printf("Sending weather data to service %s\n",
							sitr->cfg.host);
				send_to(sitr, &wd);
			}
		}

		sleep(60);
	}

	return 0;
}




