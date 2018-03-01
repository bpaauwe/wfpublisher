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
#include <math.h>
#include "wfp.h"

/*
 * Display weather data to screen.
 *
 * Display all of the collected/derrived weather data to
 * the screen.
 *
 * Currently, this is for debugging purposes to provide an
 * interactive view.  However, this could eventually be used
 * to provide some type of GUI output.
 */
static void display_wd(struct cfg_info *cfg, struct station_info *station,
					weather_data_t *wd)
{
	struct sensor_list *list = wd->tower_list;
	char t_str[4];
	char s_str[5];
	char r_str[4];
	char p_str[7];
	char d_str[7];
	char trend[8];

	if (cfg->metric) {
		sprintf(t_str, "°C");
		sprintf(s_str, " m/s");
		sprintf(r_str, " mm");
		sprintf(p_str, " mb");
		sprintf(d_str, " km");
	} else {
		unit_convert(wd, CONVERT_ALL);
		sprintf(t_str, "°F");
		sprintf(s_str, " mph");
		sprintf(r_str, " in");
		sprintf(p_str, " in/hg");
		sprintf(d_str, " miles");
	}

	if (wd->trend > 0)
		sprintf(trend, "raising");
	else if (wd->trend < 0)
		sprintf(trend, "falling");
	else
		sprintf(trend, "steady");

	/* Format the output to fit nicely on the display */
	printf("%c[2J%c[;H", 27, 27); /* clear the screen */
	printf("%s  -- Last Update: %s\n\n", station->name, wd->timestamp);

	printf("Temperature:    %5.1f%s       High:        %5.1f%s       Low:        %5.1f%s\n",
			wd->temperature, t_str, wd->temperature_high, t_str, wd->temperature_low, t_str);
	printf("Dew point:      %5.1f%s       Windchill:   %5.1f%s       Heat index: %5.1f%s\n\n",
			wd->dewpoint, t_str, wd->windchill, t_str, wd->heatindex, t_str);

	printf("Pressure:      %6.1f%-6s   Humidity:    %5.1f%%        Feels like: %5.1f%s\n\n",
			wd->pressure, p_str, wd->humidity, wd->feelslike, t_str);

	printf("Wind speed:     %5.1f%s     Wind dir:    %5.0f° (%s)\n",
			wd->windspeed, s_str, wd->winddirection, wd->wind_dir);
	printf("Gust speed:     %5.1f%s     Gust dir:    %5.0f°\n\n",
			wd->gustspeed, s_str, wd->gustdirection);

	printf("Illumination:   %5.1f Lux     Solar Rad:   %5.01f W/m^2   UV index:   %5.0f\n\n",
			wd->illumination, wd->solar, wd->uv);

	printf("Rain:          %6.2f%s      Rain 1hr:   %6.2f%s      Rain 24hrs:%6.2f%s\n",
			wd->rain, r_str, wd->rainfall_60min, r_str, wd->rainfall_24hr,
			r_str);
	printf("Daily rain:    %6.2f%s      Monthly:    %6.2f%s      Yearly:    %6.2f%s\n\n",
			wd->rainfall_day, r_str, wd->rainfall_month, r_str,
			wd->rainfall_year, r_str);

	printf("Pressure trend: %7s       Lighting:    %5d         Distance:  %5.1f%s\n\n",
			trend, wd->strikes, wd->distance, d_str);

	while(list) {
		printf("Sensor:       %9.9s       Humidity:    %5.1f%%\n",
				list->sensor->location, list->sensor->humidity);
		printf("Temperature:    %5.1f%s       High:        %5.1f%s       Low:        %5.1f%s\n\n",
				list->sensor->temperature, t_str,
				list->sensor->temperature_high, t_str,
				list->sensor->temperature_low, t_str);
		list = list->next;
	}

	printf("-------------------------------------------------------------------------------\n");

	return;
}

static int display_init(struct cfg_info *cfg, int d)
{
	return 0;
}

static const struct publisher_funcs display_funcs = {
	.init = display_init,
	.update = display_wd,
	.cleanup = NULL
};

void display_setup(struct service_info *sinfo)
{
	sinfo->funcs = display_funcs;
	return;
}

