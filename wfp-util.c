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
#include <math.h>
#include "wfp.h"

char *time_stamp(int gmt, int mode);
void send_url(char *host, int port, char *url, char *ident, int response);
char *resolve_host(char *host);
char *resolve_host_ip6(char *host);
double TempC(double tempf);

static int verbose = 0;

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

char *resolve_host(char *host)
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
char *resolve_host_ip6(char *host)
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



/*
 * Calculate the pressure trend
 *   - raising / falling / steady
 */
int calc_pressure_trend(void) {
	return 0.0;
}

/*
 * Calculate the "feels like" temperature.
 * Returns temp in F
 */
double calc_feelslike(double temp, double speed, double humidity) {
	if (TempF(temp) >= 80)
		return calc_heatindex(temp, humidity);
	else if (TempF(temp) < 50)
		return calc_windchill(temp, speed);
	else
		return temp;
}

/*
 * Calculate the windchill temperature.
 * Returns temp in C
 */
double calc_windchill(double temp, double speed) {
	double t = TempF(temp);
	double v = MS2MPH(speed);

	if ((t < 50.0) && (v > 5.0))
		return TempC(35.74 + (0.6215 * t) - (35.75 * pow(v, 0.16)) + (0.4275 * t * pow(v, 0.16)));
	else
		return temp;
}

/*
 * Calculate the dew point temperature.
 * Returns temp in C
 */
double calc_dewpoint(double t, double humidity) {
	double b;
	double h;

	b = (17.625 * t) / (243.04 + t);
	h = log(humidity/100);

	return (243.04 * (h + b)) / (17.625 - h - b);

}

/*
 * Calculates the heat index temperature.
 * Returns temp in C
 */
double calc_heatindex(double tc, double h) {
	double c1 = -42.379;
	double c2 = 2.04901523;
	double c3 = 10.14333127;
	double c4 = -0.22475541;
	double c5 = -6.83783 * pow(10, -3);
	double c6 = -5.481717 * pow(10, -2);
	double c7 = 1.22874 * pow(10, -3);
	double c8 = 8.5282 * pow(10, -4);
	double c9 = -1.99 * pow(10, -6);
	double t;

	t = TempF(tc);

	if ((t < 80.0) || (h < 40.0))
		return tc;
	else
		return TempC((c1 + (c2 * t) + (c3 * h) + (c4 * t * h) + (c5 * t * t) + (c6 * h * h) + (c7 * t * t * h) + (c8 * t * h * h) + (c9 * t * t * h * h)));
}


/* Conversion functions */
double TempF(double tempc) {
	return (tempc * 1.8) + 32;
}

double TempC(double tempf) {
	return (tempf  - 32) / 1.8;
}

double MS2MPH(double ms) {
	return ms / 0.44704;
}

double mb2in(double mb) {
	return mb * 0.02952998751;
}

double km2miles(double km) {
	return km / 1.609344;
}

double mm2inch(double mm) {
	return mm * 0.03937;
}

/*
 * Convert all data from metric to english units. The conversion
 * is done in-place on the data structure.
 */
void unit_convert(weather_data_t *wd, unsigned int skip)
{
	/* convert temperature from C to F */
	wd->temperature = TempF(wd->temperature);
	wd->temperature_high = TempF(wd->temperature_high);
	wd->temperature_low = TempF(wd->temperature_low);
	wd->dewpoint = TempF(wd->dewpoint);
	wd->heatindex = TempF(wd->heatindex);
	wd->windchill = TempF(wd->windchill);
	wd->feelslike = TempF(wd->feelslike);

	/* convert speeds from m/s to mph */
	wd->windspeed = MS2MPH(wd->windspeed);
	wd->gustspeed = MS2MPH(wd->gustspeed);

	if (!(skip & NO_PRESSURE)) {
		/* Pressure from mb to in/hg */
		wd->pressure = mb2in(wd->pressure);
	}

	/* distance from km to miles */
	wd->distance = km2miles(wd->distance);

	/* rain from mm to inches */
	wd->rain = mm2inch(wd->rain);
	wd->daily_rain = mm2inch(wd->daily_rain);
	wd->rainfall_1hr = mm2inch(wd->rainfall_1hr);
	wd->rainfall_day = mm2inch(wd->rainfall_day);
	wd->rainfall_month = mm2inch(wd->rainfall_month);
	wd->rainfall_year = mm2inch(wd->rainfall_year);
	wd->rainfall_60min = mm2inch(wd->rainfall_60min);
	wd->rainfall_24hr = mm2inch(wd->rainfall_24hr);
}

