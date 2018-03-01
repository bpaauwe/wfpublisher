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
 *
 * Publish weather data to a MQTT broker.
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
#include <stdbool.h>
#include "wfp.h"
#include <mosquitto.h>

extern double TempF(double c);
extern double MS2MPH(double ms);
extern double mb2in(double mb);

struct mosquitto *mosq = NULL;

static int mqtt_init(struct cfg_info *cfg, int debug)
{
	int port;
	int ret;

	mosquitto_lib_init();

	/* Create runtime instance with random client ID */
	mosq = mosquitto_new(NULL, true, NULL);
	if (!mosq) {
		fprintf(stderr, "Failed to initialize a MQTT instance.\n");
		return -1;
	}

	//mosquitto_username_pw_set (mosq, cfg->name, cfg->pass);

	/* Connect to MQTT broker */
	port = atoi(cfg->extra);
	ret = mosquitto_connect(mosq, cfg->host, port, 0);
	if (ret) {
		fprintf (stderr, "Can't connect to Mosquitto broker %s\n",
				cfg->host);
		return -1;
	}

	return 0;
}

static void mqtt_publish(struct cfg_info *cfg, struct station_info *station,
						weather_data_t *wd)
{
	char buf[30];
	int ret = 0;
	struct sensor_list *list = wd->tower_list;

	if (!cfg->metric)
		unit_convert(wd, CONVERT_ALL);


	ret += mosquitto_publish(mosq, NULL, "home/climate/last_update",
			strlen(wd->timestamp), wd->timestamp, 0, false);

	sprintf(buf, "%f", wd->temperature);
	ret += mosquitto_publish(mosq, NULL, "home/climate/temperature",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->temperature_high);
	ret += mosquitto_publish(mosq, NULL, "home/climate/high_temperature",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->temperature_low);
	ret += mosquitto_publish(mosq, NULL, "home/climate/low_temperature",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->humidity);
	ret += mosquitto_publish(mosq, NULL, "home/climate/humidity",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->pressure);
	ret += mosquitto_publish(mosq, NULL, "home/climate/pressure",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->pressure_sealevel);
	ret += mosquitto_publish(mosq, NULL, "home/climate/sealevel",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->trend);
	ret += mosquitto_publish(mosq, NULL, "home/climate/pressure_trend",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->windspeed);
	ret += mosquitto_publish(mosq, NULL, "home/climate/wind_speed",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->gustspeed);
	ret += mosquitto_publish(mosq, NULL, "home/climate/gust_speed",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->winddirection);
	ret += mosquitto_publish(mosq, NULL, "home/climate/wind_direction",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->gustdirection);
	ret += mosquitto_publish(mosq, NULL, "home/climate/gust_direction",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->dewpoint);
	ret += mosquitto_publish(mosq, NULL, "home/climate/dewpoint",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->heatindex);
	ret += mosquitto_publish(mosq, NULL, "home/climate/heat_index",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->windchill);
	ret += mosquitto_publish(mosq, NULL, "home/climate/windchill",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->feelslike);
	ret += mosquitto_publish(mosq, NULL, "home/climate/feels_like",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->illumination);
	ret += mosquitto_publish(mosq, NULL, "home/climate/illumination",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->solar);
	ret += mosquitto_publish(mosq, NULL, "home/climate/solar_radiation",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->uv);
	ret += mosquitto_publish(mosq, NULL, "home/climate/UV_index",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%d", wd->strikes);
	ret += mosquitto_publish(mosq, NULL, "home/climate/lightning_strikes",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->distance);
	ret += mosquitto_publish(mosq, NULL, "home/climate/lightning_distance",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->rain);
	ret += mosquitto_publish(mosq, NULL, "home/climate/rain",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->daily_rain);
	ret += mosquitto_publish(mosq, NULL, "home/climate/daily_rain",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->rainfall_1hr);
	ret += mosquitto_publish(mosq, NULL, "home/climate/hour_rain",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->rainfall_day);
	ret += mosquitto_publish(mosq, NULL, "home/climate/day_rain",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->rainfall_month);
	ret += mosquitto_publish(mosq, NULL, "home/climate/month_rain",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->rainfall_year);
	ret += mosquitto_publish(mosq, NULL, "home/climate/year_rain",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->rainfall_60min);
	ret += mosquitto_publish(mosq, NULL, "home/climate/rain_60min",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->rainfall_24hr);
	ret += mosquitto_publish(mosq, NULL, "home/climate/rain_24hr",
			strlen(buf), buf, 0, false);

	sprintf(buf, "%s", wd->wind_dir);
	ret += mosquitto_publish(mosq, NULL, "home/climate/wind_dir_text",
			strlen(buf), buf, 0, false);

	ret += mosquitto_publish(mosq, NULL, "home/climate/station",
			strlen(station->name), station->name, 0, false);
	ret += mosquitto_publish(mosq, NULL, "home/climate/location",
			strlen(station->location), station->location, 0, false);
	ret += mosquitto_publish(mosq, NULL, "home/climate/latitude",
			strlen(station->latitude), station->latitude, 0, false);
	ret += mosquitto_publish(mosq, NULL, "home/climate/longitude",
			strlen(station->longitude), station->longitude, 0, false);
	sprintf(buf, "%d", station->elevation);
	ret += mosquitto_publish(mosq, NULL, "home/climate/elevation",
			strlen(buf), buf, 0, false);

	while(list) {
		char topic[80];

		sprintf(topic, "home/%s/temperature", list->sensor->location);
		sprintf(buf, "%f", list->sensor->temperature);
		ret += mosquitto_publish(mosq, NULL, topic, strlen(buf), buf, 0, false);

		sprintf(topic, "home/%s/high_temperature", list->sensor->location);
		sprintf(buf, "%f", list->sensor->temperature_high);
		ret += mosquitto_publish(mosq, NULL, topic, strlen(buf), buf, 0, false);

		sprintf(topic, "home/%s/low_temperature", list->sensor->location);
		sprintf(buf, "%f", list->sensor->temperature_low);
		ret += mosquitto_publish(mosq, NULL, topic, strlen(buf), buf, 0, false);

		sprintf(topic, "home/%s/humidity", list->sensor->location);
		sprintf(buf, "%f", list->sensor->humidity);
		ret += mosquitto_publish(mosq, NULL, topic, strlen(buf), buf, 0, false);

		list = list->next;
	}

	if (ret)
		fprintf(stderr, "Publishing failed %d times\n", ret);

	return;
}

static void mqtt_disconnect(void)
{
	mosquitto_disconnect (mosq);
	mosquitto_destroy (mosq);
	mosquitto_lib_cleanup();

	return;
}

static const struct publisher_funcs mqtt_funcs = {
	.init = mqtt_init,
	.update = mqtt_publish,
	.cleanup = mqtt_disconnect
};

void mqtt_setup(struct service_info *sinfo)
{
	sinfo->funcs = mqtt_funcs;
	return;
}

