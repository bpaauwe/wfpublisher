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

int mqtt_init(struct cfg_info *cfg, int debug)
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

void mqtt_publish(struct cfg_info *cfg, weather_data_t *wd)
{
	char buf[20];
	int ret;

	sprintf(buf, "%f", TempF(wd->temperature));
	ret = mosquitto_publish(mosq, NULL, "home/climate/Temperature", strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->humidity);
	ret = mosquitto_publish(mosq, NULL, "home/climate/Humidity", strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->pressure);
	ret = mosquitto_publish(mosq, NULL, "home/climate/Pressure", strlen(buf), buf, 0, false);

	sprintf(buf, "%f", MS2MPH(wd->windspeed));
	ret = mosquitto_publish(mosq, NULL, "home/climate/Windspeed", strlen(buf), buf, 0, false);

	sprintf(buf, "%f", MS2MPH(wd->gustspeed));
	ret = mosquitto_publish(mosq, NULL, "home/climate/Gustspeed", strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->winddirection);
	ret = mosquitto_publish(mosq, NULL, "home/climate/Winddirection", strlen(buf), buf, 0, false);

	sprintf(buf, "%f", TempF(wd->dewpoint));
	ret = mosquitto_publish(mosq, NULL, "home/climate/Dewpoint", strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->solar);
	ret = mosquitto_publish(mosq, NULL, "home/climate/SolarRadition", strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->uv);
	ret = mosquitto_publish(mosq, NULL, "home/climate/UVIndex", strlen(buf), buf, 0, false);

	sprintf(buf, "%f", wd->rain);
	ret = mosquitto_publish(mosq, NULL, "home/climate/Rain", strlen(buf), buf, 0, false);

	pthread_exit(NULL);
	return;
}

void mqtt_disconnect(void)
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

