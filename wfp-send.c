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

	(t->sinfo->funcs.update)(&t->sinfo->cfg, t->data);

	free(t);
	return NULL;
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
	wd_copy = malloc(sizeof(weather_data_t));
	if (!wd_copy) {
		fprintf(stderr, "Failed to allocate memory for data copy\n");
		return;
	}
	memcpy(wd_copy, wd, sizeof(weather_data_t));

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
