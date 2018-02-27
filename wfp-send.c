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

extern void *send_to_wunderground(struct cfg_info *cfg, weather_data_t *data);
extern void *send_to_weatherbug(struct cfg_info *cfg, weather_data_t *data);
extern void *send_to_cwop(struct cfg_info *cfg, weather_data_t *data);
extern void *send_to_pws(struct cfg_info *cfg, weather_data_t *data);
extern void *send_to_log(struct cfg_info *cfg, weather_data_t *data);
extern void *send_to_db(struct cfg_info *cfg, weather_data_t *data);
extern void *mqtt_publish(struct cfg_info *cfg, weather_data_t *data);

static void wdfree(weather_data_t *wd);

extern int debug;
extern int verbose;

extern char *resolve_host(char *host);
extern char *resolve_host_ip6(char *host);

struct thread_info {
	struct service_info *sinfo;
	weather_data_t *data;
};

static int send_count = 0;

/*
 * Helper function to call the publisher update function
 * from withing a separate thread.
 */
void *invoke_publisher(void *data)
{
	struct thread_info *t = (struct thread_info *)data;

	(t->sinfo->funcs.update)(&t->sinfo->cfg, &t->sinfo->station, t->data);

	wdfree(t->data);
	free(t);
	return NULL;
}

/*
 * Make a copy of the weather data structure.
 */
static weather_data_t *wdcopy(weather_data_t *wd)
{
	weather_data_t *cpy;
	struct sensor_list *list, *list_cp;

	cpy = malloc(sizeof(weather_data_t));
	if (!cpy) {
		fprintf(stderr, "Failed to allocate memory for data copy\n");
		return NULL;
	}

	memcpy(cpy, wd, sizeof(weather_data_t));
	cpy->tower_list = NULL;
	cpy->timestamp = NULL;

	/* Copy the timestamp */
	if (wd->timestamp)
		cpy->timestamp = strdup(wd->timestamp);

	/* Copy sensor data */
	list = wd->tower_list;
	while (list) {
		list_cp = malloc(sizeof(struct sensor_list));
		list_cp->sensor = malloc(sizeof(struct sensor_data));
		memcpy(list_cp->sensor, list->sensor, sizeof(struct sensor_data));

		/* should we copy the timestamp and sensor_id? */

		/* Add new entry to front of list */
		list_cp->next = cpy->tower_list;
		cpy->tower_list = list_cp;

		list = list->next;
	}

	return cpy;
}

static void wdfree(weather_data_t *wd)
{
	struct sensor_list *list;

	/* Free the timestamp */
	if (wd->timestamp)
		free(wd->timestamp);

	/* Free sensor data */
	list = wd->tower_list;
	while (wd->tower_list) {
		list = wd->tower_list;
		wd->tower_list = wd->tower_list->next;
		free(list->sensor);
		free(list);
	}

	free(wd);
}


void send_to(struct service_info *sinfo, weather_data_t *wd)
{
	weather_data_t *wd_copy;
	pthread_t w_thread;
	struct thread_info *tinfo;
	int err = 1;
	char *ts;

	if (sinfo == NULL)
		goto end;

	send_count++;

	/* Make a copy of the data so it doesn't get overwritten */
	wd_copy = wdcopy(wd);
	if (!wd_copy)
		return;

	tinfo = malloc(sizeof(struct thread_info));
	tinfo->sinfo = sinfo;
	tinfo->data = wd_copy;

	err = pthread_create(&w_thread, NULL, invoke_publisher, (void *)tinfo);

	if (err) {
		free(tinfo);
		free(wd_copy);
		ts = time_stamp(0, 1);
		fprintf(stderr, "%s: Failed to create thread for %s (cnt=%d): %s\n",
				ts, sinfo->cfg.name, send_count, strerror(err));
		free(ts);
	}

	pthread_detach(w_thread);
end:
	return;
}
