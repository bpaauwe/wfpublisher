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

extern void *send_to_wunderground(void *data);
extern void *send_to_weatherbug(void *data);
extern void *send_to_cwop(void *data);
extern void *send_to_pws(void *data);
extern void *send_to_log(void *data);
extern void *send_to_db(void *data);
extern void *mqtt_publish(void *data);

extern int debug;
extern int verbose;
extern struct service_info sinfo[7];

extern char *resolve_host(char *host);
extern char *resolve_host_ip6(char *host);

static void *(*send_to_table[7])(void *data) = {
	send_to_log,
	send_to_wunderground,
	send_to_weatherbug,
	send_to_cwop,
	send_to_pws,
	send_to_db,
	mqtt_publish,
};

static int send_count = 0;

void send_to(int service, weather_data_t *wd)
{
	weather_data_t *wd_copy;
	pthread_t w_thread;
	int err = 1;
	char *ts;

	if (service >= SERVICE_END)
		goto end;

	send_count++;

	/* Make a copy of the data so it doesn't get overwritten */
	wd_copy = malloc(sizeof(weather_data_t));
	if (!wd_copy) {
		fprintf(stderr, "Failed to allocate memory for data copy\n");
		return;
	}
	memcpy(wd_copy, wd, sizeof(weather_data_t));

	err = pthread_create(&w_thread, NULL, send_to_table[service],
			(void *)wd_copy);

	if (err) {
		free(wd_copy);
		ts = time_stamp(0, 1);
		fprintf(stderr, "%s: Failed to create thread for %s (cnt=%d): %s\n",
				ts, sinfo[service].cfg.name, send_count, strerror(err));
		free(ts);
	}

	pthread_detach(w_thread);
end:
	return;
}
