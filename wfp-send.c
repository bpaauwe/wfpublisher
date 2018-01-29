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

extern int debug;
extern int verbose;
extern bool skip_wu;
extern bool skip_wb;
extern bool skip_cw;
extern bool skip_pws;


static char *resolve_host(char *host);
static char *resolve_host_ip6(char *host);
char *time_stamp(int gmt, int mode);

static int send_count = 0;

void send_to(int service, weather_data_t *wd)
{
	weather_data_t *wd_copy;
	pthread_t w_thread;
	int err = 1;
	char *ts;

	send_count++;

	/* Make a copy of the data so it doesn't get overwritten */
	wd_copy = malloc(sizeof(weather_data_t));
	if (!wd_copy) {
		fprintf(stderr, "Failed to allocate memory for data copy\n");
		return;
	}
	memcpy(wd_copy, wd, sizeof(weather_data_t));

	switch(service) {
		case WUNDERGROUND:
			if (!skip_wu)
				err = pthread_create(&w_thread, NULL, send_to_wunderground,
						(void *)wd_copy);
			break;
		case WEATHERBUG:
			if (!skip_wb)
				err = pthread_create(&w_thread, NULL, send_to_weatherbug,
						(void *)wd_copy);
			break;
		case CWOP:
			if (!skip_cw)
				err = pthread_create(&w_thread, NULL, send_to_cwop,
						(void *)wd_copy);
			break;
		case PWS:
			if (!skip_pws)
				err = pthread_create(&w_thread, NULL, send_to_pws,
						(void *)wd_copy);
			break;
		default:
			fprintf(stderr, "send_to: Unknown weather service %d\n", service);
			free(wd_copy);
			return;
	}

	if (err) {
		free(wd_copy);
		ts = time_stamp(0, 1);
		fprintf(stderr, "%s: Failed to create thread to service %d (cnt=%d): %s\n",
				ts, service, send_count, strerror(err));
		free(ts);
	}

	pthread_detach(w_thread);
	return;
}


void send_url(char *host, int port, char *url, char *ident, int response)
{
	int sock;
	struct sockaddr_in *remote;
	int tmpres;
	char *ip_addr;
	char *buf;
	char *ts;

	if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		fprintf(stderr, "ERROR: Failed to create TCP socket.\n");
		return;
	}

	ip_addr = resolve_host(host);
	if (!ip_addr) {
		fprintf(stderr, "ERROR: Failed to resolve %s\n", host);
		close(sock);
		return;
	}

	if (strcmp(ip_addr, "127.0.1.1") == 0) {
		free(ip_addr);
		ip_addr = resolve_host_ip6(host);
		if (strcmp(ip_addr, "127.0.1.1") == 0) {
			fprintf(stderr, "ERROR: failed to resolve ip address for %s\n",
					host);
			close(sock);
			free(ip_addr);
			return;
		}
	}


	remote = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in *));
	remote->sin_family = AF_INET;
	tmpres = inet_pton(AF_INET, ip_addr, (void *)(&(remote->sin_addr.s_addr)));
	if (tmpres) {
	}

	remote->sin_port = htons(port);

	if (connect(sock, (struct sockaddr *)remote, sizeof(struct sockaddr)) < 0) {
		ts = time_stamp(0, 1);
		fprintf(stderr, "ERROR: %s %s(%s) failed: %m\n", ts, host, ip_addr);
		free(ts);
		free(remote);
		free(ip_addr);
		return;
	}

	if (ident) {
		send(sock, ident, strlen(ident), 0);
		sleep(2);
	}

	//printf("Sending: %s\n", url);
	send(sock, url, strlen(url), 0);

	/* should we wait for a response from the server? */
	if (response) {
		if (verbose > 1)
			fprintf(stderr, "\nRemote returned:\n");
		buf = (char *)malloc(4096);
		while ((tmpres = recv(sock, buf, 4095, 0)) > 0) {
			buf[tmpres] = '\0';
			if (verbose > 1)
				fprintf(stderr, "%s", buf);
		}
		free(buf);
	}

	if (verbose > 1)
		fprintf(stderr, "Closing connection.\n");

	close(sock);
	free(remote);
	free(ip_addr);
}

static char *resolve_host(char *host)
{
	struct hostent *hent;
	struct in_addr **addr_list;
	int iplen = 15;
	char *ip = (char *)malloc(iplen + 1);
	int i;

	memset(ip, 0, iplen+1);

	if ((hent = gethostbyname(host)) == NULL) {
		fprintf(stderr, "Failed to resolve host name.\n");
		free(ip);
		return NULL;
	}

	addr_list = (struct in_addr **) hent->h_addr_list;

	if (verbose > 1) {
		for (i = 0; addr_list[i] != NULL; i++) {
			fprintf(stderr, "RESOLVE: %s[%d] = %s\n",
					host, i, inet_ntoa(*addr_list[i]));
		}
	}

	if (inet_ntop(AF_INET, (void *)hent->h_addr_list[0], ip, iplen) == NULL) {
		fprintf(stderr, "Failed to resolve host name.\n");
		return NULL;
	}

	return ip;
}


/*
 * Really should be using this but since it seems to want a service
 * and I'm not sure if the 'http' service is valid for all weather
 * services, I'm only going to call this if gethostbyname() fails.
 */
static char *resolve_host_ip6(char *host)
{
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_in *h;
	int rv;
	char *ip;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if ((rv = getaddrinfo(host, "http", &hints, &servinfo)) != 0) {
		fprintf(stderr, "resolve_host: %s\n", gai_strerror(rv));
		return NULL;
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		h = (struct sockaddr_in *)p->ai_addr;
		ip = malloc(strlen(inet_ntoa(h->sin_addr)));
		if (!ip)
			goto out;
		strcpy(ip, inet_ntoa(h->sin_addr));
		goto out;
	}

out:
	freeaddrinfo(servinfo);
	return ip;
}




/*
 * Generate a readable time stamp string.
 *
 * @gmt - if one use GMT, else use local time zone
 * @mode - if one generate string for error logging, else for url
 */

char *time_stamp(int gmt, int mode)
{
	time_t t = time(NULL);
	struct tm gt;
	char *ts = (char *)malloc(25);;

	if (gmt) {
		gmtime_r(&t, &gt);
	} else {
		localtime_r(&t, &gt);
	}

	if (mode) {
		sprintf(ts, "%4d-%02d-%02d %02d:%02d:%02d",
				gt.tm_year + 1900, gt.tm_mon + 1, gt.tm_mday,
				gt.tm_hour, gt.tm_min, gt.tm_sec);
	} else {
		sprintf(ts, "%4d-%02d-%02d%%20%02d:%02d:%02d",
				gt.tm_year + 1900, gt.tm_mon + 1, gt.tm_mday,
				gt.tm_hour, gt.tm_min, gt.tm_sec);
	}

	return ts;
}
