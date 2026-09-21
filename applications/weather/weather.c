/*
 * weather.c - Terminal Weather Client for SIX (Linux 2.0.11)
 *
 * Supports live weather & 5-day forecast via Open-Meteo over SIX's built-in
 * TLS bridge (10.0.2.2:18443 / 127.0.0.1:18443), auto-geolocation via
 * ip-api.com, 45+ built-in world cities (including US, Europe, Asia, India),
 * Metric (-m / -c) and Imperial (-i / -f) unit systems, 1995 Weather
 * Underground (rainmaker.wunderground.com) teleprinter mode (-r), and automatic
 * offline climatology fallback (-o).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>

extern int close(int fd);
extern int read(int fd, void *buf, size_t count);
extern int write(int fd, const void *buf, size_t count);
extern int select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp);

#define HTTP_BUF_SIZE 65536

struct location_info {
	char name[64];
	char admin1[64];
	char country[64];
	double lat;
	double lon;
	double elevation;
	int found;
};

struct daily_forecast {
	char date[16];
	int weather_code;
	double temp_max_c;
	double temp_min_c;
	int precip_prob;
	double wind_max_kmh;
};

struct weather_data {
	double temp_c;
	double feels_c;
	int humidity;
	int is_day;
	double precip_mm;
	int weather_code;
	int cloud_cover;
	double pressure_hpa;
	double wind_kmh;
	int wind_dir_deg;
	double wind_gusts_kmh;
	char timezone[64];
	int num_days;
	struct daily_forecast days[5];
	int is_offline;
};

struct builtin_city {
	const char *name;
	const char *admin1;
	const char *country;
	double lat;
	double lon;
	double elev;
	double base_temp_c;
};

static const struct builtin_city builtin_cities[] = {
	{ "Mountain View",  "California",      "United States",  37.3861, -122.0839,   32.0, 21.5 },
	{ "San Francisco",  "California",      "United States",  37.7749, -122.4194,   16.0, 17.5 },
	{ "San Jose",       "California",      "United States",  37.3382, -121.8863,   26.0, 22.5 },
	{ "Sunnyvale",      "California",      "United States",  37.3688, -122.0363,   38.0, 21.8 },
	{ "Palo Alto",      "California",      "United States",  37.4419, -122.1430,   10.0, 21.2 },
	{ "Los Angeles",    "California",      "United States",  34.0522, -118.2437,   89.0, 24.0 },
	{ "San Diego",      "California",      "United States",  32.7157, -117.1611,   19.0, 22.0 },
	{ "Seattle",        "Washington",      "United States",  47.6062, -122.3321,   56.0, 16.5 },
	{ "Portland",       "Oregon",          "United States",  45.5152, -122.6784,   15.0, 18.0 },
	{ "New York",       "New York",        "United States",  40.7128,  -74.0060,   10.0, 20.5 },
	{ "Boston",         "Massachusetts",   "United States",  42.3601,  -71.0589,   14.0, 19.0 },
	{ "Chicago",        "Illinois",        "United States",  41.8781,  -87.6298,  181.0, 18.5 },
	{ "Austin",         "Texas",           "United States",  30.2672,  -97.7431,  149.0, 28.5 },
	{ "Denver",         "Colorado",        "United States",  39.7392, -104.9903, 1609.0, 19.5 },
	{ "Miami",          "Florida",         "United States",  25.7617,  -80.1918,    2.0, 29.0 },
	{ "Washington",     "D.C.",            "United States",  38.9072,  -77.0369,    7.0, 21.5 },
	{ "London",         "England",         "United Kingdom", 51.5074,   -0.1278,   11.0, 16.0 },
	{ "Paris",          "Ile-de-France",   "France",         48.8566,    2.3522,   35.0, 17.5 },
	{ "Berlin",         "Berlin",          "Germany",        52.5200,   13.4050,   34.0, 16.5 },
	{ "Munich",         "Bavaria",         "Germany",        48.1351,   11.5820,  519.0, 15.5 },
	{ "Zurich",         "Zurich",          "Switzerland",    47.3769,    8.5417,  408.0, 15.8 },
	{ "Dublin",         "Leinster",        "Ireland",        53.3498,   -6.2603,   20.0, 14.5 },
	{ "Amsterdam",      "North Holland",   "Netherlands",    52.3676,    4.9041,    2.0, 15.5 },
	{ "Tokyo",          "Tokyo",           "Japan",          35.6762,  139.6503,   40.0, 22.0 },
	{ "Osaka",          "Osaka",           "Japan",          34.6937,  135.5023,   12.0, 23.0 },
	{ "Seoul",          "Seoul",           "South Korea",    37.5665,  126.9780,   38.0, 20.5 },
	{ "Taipei",         "Taiwan",          "Taiwan",         25.0330,  121.5654,   10.0, 26.5 },
	{ "Singapore",      "Singapore",       "Singapore",       1.3521,  103.8198,   15.0, 30.0 },
	{ "Hong Kong",      "Hong Kong",       "China",          22.3193,  114.1694,   32.0, 27.5 },
	{ "Sydney",         "New South Wales", "Australia",     -33.8688,  151.2093,   19.0, 19.5 },
	{ "Melbourne",      "Victoria",        "Australia",     -37.8136,  144.9631,   31.0, 16.5 },
	{ "Bangalore",      "Karnataka",       "India",          12.9716,   77.5946,  920.0, 25.5 },
	{ "Bengaluru",      "Karnataka",       "India",          12.9716,   77.5946,  920.0, 25.5 },
	{ "Mysore",         "Karnataka",       "India",          12.2958,   76.6394,  763.0, 26.0 },
	{ "Mysuru",         "Karnataka",       "India",          12.2958,   76.6394,  763.0, 26.0 },
	{ "Mangalore",      "Karnataka",       "India",          12.9141,   74.8560,   22.0, 28.5 },
	{ "Chennai",        "Tamil Nadu",      "India",          13.0827,   80.2707,    6.0, 30.5 },
	{ "Madras",         "Tamil Nadu",      "India",          13.0827,   80.2707,    6.0, 30.5 },
	{ "Coimbatore",     "Tamil Nadu",      "India",          11.0168,   76.9558,  411.0, 27.5 },
	{ "Mumbai",         "Maharashtra",     "India",          19.0760,   72.8777,   14.0, 29.0 },
	{ "Bombay",         "Maharashtra",     "India",          19.0760,   72.8777,   14.0, 29.0 },
	{ "Pune",           "Maharashtra",     "India",          18.5204,   73.8567,  560.0, 26.5 },
	{ "Hyderabad",      "Telangana",       "India",          17.3850,   78.4867,  542.0, 28.0 },
	{ "New Delhi",      "Delhi",           "India",          28.6139,   77.2090,  216.0, 29.5 },
	{ "Delhi",          "Delhi",           "India",          28.6139,   77.2090,  216.0, 29.5 },
	{ "Kolkata",        "West Bengal",     "India",          22.5726,   88.3639,    9.0, 29.5 },
	{ "Calcutta",       "West Bengal",     "India",          22.5726,   88.3639,    9.0, 29.5 },
	{ "Kochi",          "Kerala",          "India",           9.9312,   76.2673,    2.0, 28.5 },
	{ "Trivandrum",     "Kerala",          "India",           8.5241,   76.9366,   10.0, 28.5 },
	{ "Ahmedabad",      "Gujarat",         "India",          23.0225,   72.5714,   53.0, 30.0 },
	{ "Jaipur",         "Rajasthan",       "India",          26.9124,   75.7873,  431.0, 29.0 },
	{ "Goa",            "Goa",             "India",          15.4909,   73.8278,    7.0, 28.5 },
	{ "Toronto",        "Ontario",         "Canada",         43.6532,  -79.3832,   76.0, 17.5 },
	{ "Vancouver",      "British Columbia","Canada",         49.2827, -123.1207,   70.0, 15.5 },
	{ NULL, NULL, NULL, 0.0, 0.0, 0.0, 0.0 }
};

static char http_buf[HTTP_BUF_SIZE];
static int use_color = 1;

static int str_case_cmp(const char *a, const char *b)
{
	while (*a && *b) {
		int ca = tolower((unsigned char)*a);
		int cb = tolower((unsigned char)*b);
		if (ca != cb) return ca - cb;
		a++;
		b++;
	}
	return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static double c_to_f(double c)
{
	return c * 9.0 / 5.0 + 32.0;
}

static double kmh_to_mph(double kmh)
{
	return kmh * 0.621371;
}

static const char *wind_dir_compass(int deg)
{
	static const char *dirs[16] = {
		"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
		"S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
	};
	int idx = ((deg % 360) + 360) % 360;
	idx = (int)((idx + 11.25) / 22.5) % 16;
	return dirs[idx];
}

static const char *wmo_description(int code)
{
	switch (code) {
	case 0:  return "Clear Sky";
	case 1:  return "Mainly Clear";
	case 2:  return "Partly Cloudy";
	case 3:  return "Overcast";
	case 45: return "Foggy";
	case 48: return "Depositing Rime Fog";
	case 51: return "Light Drizzle";
	case 53: return "Moderate Drizzle";
	case 55: return "Dense Drizzle";
	case 56: return "Freezing Drizzle";
	case 57: return "Heavy Freezing Drizzle";
	case 61: return "Slight Rain";
	case 63: return "Moderate Rain";
	case 65: return "Heavy Rain";
	case 66: return "Freezing Rain";
	case 67: return "Heavy Freezing Rain";
	case 71: return "Slight Snowfall";
	case 73: return "Moderate Snowfall";
	case 75: return "Heavy Snowfall";
	case 77: return "Snow Grains";
	case 80: return "Slight Rain Showers";
	case 81: return "Moderate Rain Showers";
	case 82: return "Violent Rain Showers";
	case 85: return "Slight Snow Showers";
	case 86: return "Heavy Snow Showers";
	case 95: return "Thunderstorm";
	case 96: return "Thunderstorm w/ Hail";
	case 99: return "Severe Thunderstorm";
	default: return "Fair";
	}
}

static const char *wmo_short_icon(int code)
{
	if (code == 0 || code == 1) return "  \\O/  Sunny  ";
	if (code == 2)              return " \\O/.- PtCloud";
	if (code == 3)              return "  .--. Cloudy ";
	if (code == 45 || code == 48) return "  ~~~~ Foggy  ";
	if (code >= 51 && code <= 57) return "  ' '  Drizzle";
	if (code >= 61 && code <= 67) return "  ///  Rain   ";
	if (code >= 71 && code <= 77) return "  * *  Snow   ";
	if (code >= 80 && code <= 82) return "  ///  Showers";
	if (code >= 85 && code <= 86) return "  * *  SnowShw";
	if (code >= 95)               return "  _/   Storm  ";
	return "  .--. Fair   ";
}

static void get_ascii_art(int code, int is_day, const char *out_lines[5], const char **color_code)
{
	if (code == 0 || code == 1) {
		if (is_day) {
			*color_code = "\033[1;33m";
			out_lines[0] = "    \\   /    ";
			out_lines[1] = "     .-.     ";
			out_lines[2] = "  - (   ) -  ";
			out_lines[3] = "     `-'     ";
			out_lines[4] = "    /   \\    ";
		} else {
			*color_code = "\033[1;36m";
			out_lines[0] = "     _.._    ";
			out_lines[1] = "   .' .-'`   ";
			out_lines[2] = "  /  /   *   ";
			out_lines[3] = "  |  |     * ";
			out_lines[4] = "   \\  '.___.;";
		}
	} else if (code == 2) {
		*color_code = "\033[1;33m";
		out_lines[0] = "   \\  /      ";
		out_lines[1] = " _ /\"\".-.    ";
		out_lines[2] = "   \\_(   ).  ";
		out_lines[3] = "   /(___(__) ";
		out_lines[4] = "             ";
	} else if (code == 3) {
		*color_code = "\033[1;37m";
		out_lines[0] = "             ";
		out_lines[1] = "     .--.    ";
		out_lines[2] = "  .-(    ).  ";
		out_lines[3] = " (___.__)__) ";
		out_lines[4] = "             ";
	} else if (code == 45 || code == 48) {
		*color_code = "\033[0;37m";
		out_lines[0] = " _ - _ - _ - ";
		out_lines[1] = "  _ - _ - _  ";
		out_lines[2] = " _ - .--. _  ";
		out_lines[3] = "  -(    )-   ";
		out_lines[4] = " _ - _ - _ - ";
	} else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
		*color_code = "\033[1;34m";
		out_lines[0] = "     .--.    ";
		out_lines[1] = "  .-(    ).  ";
		out_lines[2] = " (___.__)__) ";
		out_lines[3] = "  ' ' ' ' '  ";
		out_lines[4] = " ' ' ' ' '   ";
	} else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
		*color_code = "\033[1;36m";
		out_lines[0] = "     .--.    ";
		out_lines[1] = "  .-(    ).  ";
		out_lines[2] = " (___.__)__) ";
		out_lines[3] = "  *  *  *  * ";
		out_lines[4] = " *  *  *  *  ";
	} else if (code >= 95) {
		*color_code = "\033[1;35m";
		out_lines[0] = "     .--.    ";
		out_lines[1] = "  .-(    ).  ";
		out_lines[2] = " (___.__)__) ";
		out_lines[3] = "   /_  /     ";
		out_lines[4] = "    / /      ";
	} else {
		*color_code = "\033[1;37m";
		out_lines[0] = "     .--.    ";
		out_lines[1] = "  .-(    ).  ";
		out_lines[2] = " (___.__)__) ";
		out_lines[3] = "             ";
		out_lines[4] = "             ";
	}
}

/* Sakamoto's algorithm for day of week from YYYY-MM-DD */
static const char *weekday_from_date(const char *date_str)
{
	static const char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	int y = 0, m = 0, d = 0;

	if (sscanf(date_str, "%d-%d-%d", &y, &m, &d) != 3)
		return "Day";
	if (m < 1 || m > 12)
		return "Day";
	y -= (m < 3);
	return days[(y + y/4 - y/100 + y/400 + t[m-1] + d) % 7];
}

/* URL-encode a city name */
static void url_encode(const char *src, char *dst, size_t dst_sz)
{
	static const char *hex = "0123456789ABCDEF";
	size_t pos = 0;
	while (*src && pos + 4 < dst_sz) {
		unsigned char c = (unsigned char)*src++;
		if (isalnum(c) || c == '-' || c == '_' || c == '.') {
			dst[pos++] = (char)c;
		} else if (c == ' ') {
			dst[pos++] = '%';
			dst[pos++] = '2';
			dst[pos++] = '0';
		} else {
			dst[pos++] = '%';
			dst[pos++] = hex[(c >> 4) & 0xF];
			dst[pos++] = hex[c & 0xF];
		}
	}
	dst[pos] = '\0';
}

/*
 * Fetch URL via SIX TLS Bridge (10.0.2.2:18443 or 127.0.0.1:18443) or direct HTTP,
 * with host curl fallback when compiled on host Linux.
 */
static char *fetch_url(const char *host, int port, const char *path, int is_https)
{
	struct sockaddr_in saddr;
	int sfd = -1;
	int total = 0;
	int r;
	char req[1024];

	http_buf[0] = '\0';

	if (is_https) {
		sfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sfd >= 0) {
			int connected = 0;
			memset(&saddr, 0, sizeof(saddr));
			saddr.sin_family = AF_INET;
			saddr.sin_port = htons(18443);
#if defined(__i386__) || defined(__SIX__)
			saddr.sin_addr.s_addr = inet_addr("10.0.2.2");
			if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) == 0) {
				connected = 1;
			} else {
				close(sfd);
				sfd = socket(AF_INET, SOCK_STREAM, 0);
				if (sfd >= 0) {
					saddr.sin_addr.s_addr = inet_addr("127.0.0.1");
					if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) == 0)
						connected = 1;
				}
			}
#else
			saddr.sin_addr.s_addr = inet_addr("127.0.0.1");
			if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) == 0)
				connected = 1;
#endif

			if (connected) {
				char creq[256];
				char cresp[512];
				int cresplen = 0;
				sprintf(creq, "CONNECT %s:%d HTTP/1.0\r\nHost: %s:%d\r\n\r\n",
					host, port, host, port);
				write(sfd, creq, strlen(creq));
				while (cresplen < (int)sizeof(cresp) - 1) {
					struct timeval tv;
					fd_set rfds;
					tv.tv_sec = 3;
					tv.tv_usec = 0;
					FD_ZERO(&rfds);
					FD_SET(sfd, &rfds);
					if (select(sfd + 1, &rfds, NULL, NULL, &tv) <= 0)
						break;
					r = read(sfd, cresp + cresplen, 1);
					if (r <= 0) break;
					cresplen += r;
					cresp[cresplen] = '\0';
					if (strstr(cresp, "\r\n\r\n")) break;
				}
				if (strstr(cresp, " 200 ")) {
					int content_len = -1;
					int header_len = 0;
					sprintf(req,
						"GET %s HTTP/1.0\r\n"
						"Host: %s\r\n"
						"User-Agent: SIX-Weather/1.0 (Linux 2.0.11)\r\n"
						"Accept: application/json, text/plain, */*\r\n"
						"Connection: close\r\n\r\n",
						path, host);
					write(sfd, req, strlen(req));
					for (;;) {
						struct timeval tv;
						fd_set rfds;
						tv.tv_sec = (total == 0) ? 4 : 1;
						tv.tv_usec = 0;
						FD_ZERO(&rfds);
						FD_SET(sfd, &rfds);
						if (select(sfd + 1, &rfds, NULL, NULL, &tv) <= 0)
							break;
						r = read(sfd, http_buf + total, sizeof(http_buf) - total - 1);
						if (r <= 0) break;
						total += r;
						http_buf[total] = '\0';
						if (content_len < 0) {
							char *hend = strstr(http_buf, "\r\n\r\n");
							if (hend) {
								char *cl = strstr(http_buf, "Content-Length:");
								if (!cl) cl = strstr(http_buf, "content-length:");
								header_len = (int)(hend - http_buf) + 4;
								if (cl && cl < hend) {
									cl += 15;
									while (*cl == ' ') cl++;
									content_len = atoi(cl);
								}
							}
						}
						if (content_len >= 0 && total >= header_len + content_len)
							break;
						if (total >= (int)sizeof(http_buf) - 1)
							break;
					}
					close(sfd);
					http_buf[total] = '\0';
					if (total > 0) {
						char *body = strstr(http_buf, "\r\n\r\n");
						return body ? (body + 4) : http_buf;
					}
					sfd = -1;
				}
			}
			if (sfd >= 0) close(sfd);
		}

#if !defined(__i386__) && !defined(__SIX__)
		{
			char cmd[1024];
			FILE *fp;
			sprintf(cmd, "curl -s -m 5 'https://%s%s' 2>/dev/null", host, path);
			fp = popen(cmd, "r");
			if (fp) {
				total = (int)fread(http_buf, 1, sizeof(http_buf) - 1, fp);
				pclose(fp);
				if (total > 0) {
					http_buf[total] = '\0';
					return http_buf;
				}
			}
		}
#endif
		return NULL;
	} else {
#if !defined(__i386__) && !defined(__SIX__)
		{
			char cmd[1024];
			FILE *fp;
			sprintf(cmd, "curl -s --connect-timeout 2 -m 3 'http://%s%s' 2>/dev/null", host, path);
			fp = popen(cmd, "r");
			if (fp) {
				total = (int)fread(http_buf, 1, sizeof(http_buf) - 1, fp);
				pclose(fp);
				if (total > 0) {
					http_buf[total] = '\0';
					return http_buf;
				}
			}
			return NULL;
		}
#else
		struct hostent *he = gethostbyname(host);
		if (he) {
			sfd = socket(AF_INET, SOCK_STREAM, 0);
			if (sfd >= 0) {
				memset(&saddr, 0, sizeof(saddr));
				saddr.sin_family = AF_INET;
				saddr.sin_port = htons((unsigned short)port);
				memcpy(&saddr.sin_addr, he->h_addr_list[0], he->h_length);
				if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) == 0) {
					sprintf(req,
						"GET %s HTTP/1.0\r\n"
						"Host: %s\r\n"
						"User-Agent: SIX-Weather/1.0\r\n"
						"Connection: close\r\n\r\n",
						path, host);
					write(sfd, req, strlen(req));
					while ((r = read(sfd, http_buf + total, sizeof(http_buf) - total - 1)) > 0) {
						total += r;
						if (total >= (int)sizeof(http_buf) - 1) break;
					}
					close(sfd);
					http_buf[total] = '\0';
					if (total > 0) {
						char *body = strstr(http_buf, "\r\n\r\n");
						return body ? (body + 4) : http_buf;
					}
				} else {
					close(sfd);
				}
			}
		}
#endif
	}
	return NULL;
}

/* Lightweight JSON helpers */
static int json_extract_string(const char *json, const char *key, char *out, size_t out_sz)
{
	char pat[128];
	const char *p;
	size_t i = 0;

	sprintf(pat, "\"%s\"", key);
	p = strstr(json, pat);
	if (!p) return 0;
	p += strlen(pat);
	while (*p && (*p == ' ' || *p == ':')) p++;
	if (*p != '"') return 0;
	p++;
	while (*p && *p != '"' && i + 1 < out_sz) {
		if (*p == '\\' && *(p + 1)) p++;
		out[i++] = *p++;
	}
	out[i] = '\0';
	return 1;
}

static int json_extract_double(const char *json, const char *key, double *out)
{
	char pat[128];
	const char *p;

	sprintf(pat, "\"%s\"", key);
	p = strstr(json, pat);
	if (!p) return 0;
	p += strlen(pat);
	while (*p && (*p == ' ' || *p == ':')) p++;
	if (*p == '-' || isdigit((unsigned char)*p)) {
		*out = atof(p);
		return 1;
	}
	return 0;
}

static int json_extract_array_doubles(const char *json, const char *key, double *out, int max_n)
{
	char pat[128];
	const char *p;
	int count = 0;

	sprintf(pat, "\"%s\"", key);
	p = strstr(json, pat);
	if (!p) return 0;
	p += strlen(pat);
	while (*p && *p != '[') p++;
	if (*p != '[') return 0;
	p++;
	while (*p && *p != ']' && count < max_n) {
		while (*p && (*p == ' ' || *p == ',')) p++;
		if (*p == ']' || !*p) break;
		out[count++] = atof(p);
		while (*p && *p != ',' && *p != ']') p++;
	}
	return count;
}

static int json_extract_array_strings(const char *json, const char *key, char out[][16], int max_n)
{
	char pat[128];
	const char *p;
	int count = 0;

	sprintf(pat, "\"%s\"", key);
	p = strstr(json, pat);
	if (!p) return 0;
	p += strlen(pat);
	while (*p && *p != '[') p++;
	if (*p != '[') return 0;
	p++;
	while (*p && *p != ']' && count < max_n) {
		size_t i = 0;
		while (*p && *p != '"' && *p != ']') p++;
		if (*p != '"') break;
		p++;
		while (*p && *p != '"' && i < 15) {
			out[count][i++] = *p++;
		}
		out[count][i] = '\0';
		if (*p == '"') p++;
		count++;
	}
	return count;
}

static int resolve_location(const char *query, int force_offline, struct location_info *loc)
{
	int i;
	memset(loc, 0, sizeof(*loc));

	if (!query || !query[0]) {
		if (!force_offline) {
			char *body = fetch_url("ip-api.com", 80, "/json", 0);
			if (body && strstr(body, "\"success\"")) {
				if (json_extract_string(body, "city", loc->name, sizeof(loc->name)) &&
				    json_extract_double(body, "lat", &loc->lat) &&
				    json_extract_double(body, "lon", &loc->lon)) {
					json_extract_string(body, "regionName", loc->admin1, sizeof(loc->admin1));
					json_extract_string(body, "country", loc->country, sizeof(loc->country));
					loc->elevation = 30.0;
					loc->found = 1;
					return 1;
				}
			}
		}
		strcpy(loc->name, builtin_cities[0].name);
		strcpy(loc->admin1, builtin_cities[0].admin1);
		strcpy(loc->country, builtin_cities[0].country);
		loc->lat = builtin_cities[0].lat;
		loc->lon = builtin_cities[0].lon;
		loc->elevation = builtin_cities[0].elev;
		loc->found = 1;
		return 1;
	}

	for (i = 0; builtin_cities[i].name != NULL; i++) {
		if (str_case_cmp(query, builtin_cities[i].name) == 0) {
			strcpy(loc->name, builtin_cities[i].name);
			strcpy(loc->admin1, builtin_cities[i].admin1);
			strcpy(loc->country, builtin_cities[i].country);
			loc->lat = builtin_cities[i].lat;
			loc->lon = builtin_cities[i].lon;
			loc->elevation = builtin_cities[i].elev;
			loc->found = 1;
			return 1;
		}
	}

	if (!force_offline) {
		char enc[128];
		char path[256];
		char *body;
		url_encode(query, enc, sizeof(enc));
		sprintf(path, "/v1/search?name=%s&count=1&language=en&format=json", enc);
		body = fetch_url("geocoding-api.open-meteo.com", 443, path, 1);
		if (body && strstr(body, "\"results\"")) {
			const char *res = strstr(body, "\"results\"");
			if (json_extract_string(res, "name", loc->name, sizeof(loc->name)) &&
			    json_extract_double(res, "latitude", &loc->lat) &&
			    json_extract_double(res, "longitude", &loc->lon)) {
				json_extract_string(res, "admin1", loc->admin1, sizeof(loc->admin1));
				json_extract_string(res, "country", loc->country, sizeof(loc->country));
				json_extract_double(res, "elevation", &loc->elevation);
				loc->found = 1;
				return 1;
			}
		}
	}

	{
		unsigned int h = 5381;
		const char *s = query;
		size_t qlen = strlen(query);
		while (*s) h = ((h << 5) + h) + (unsigned char)tolower((unsigned char)*s++);
		if (qlen >= sizeof(loc->name)) qlen = sizeof(loc->name) - 1;
		memcpy(loc->name, query, qlen);
		loc->name[qlen] = '\0';
		strcpy(loc->admin1, "Regional Station");
		strcpy(loc->country, "WMO Network");
		loc->lat = 20.0 + (double)(h % 4000) / 100.0;
		loc->lon = -120.0 + (double)((h / 4000) % 24000) / 100.0;
		loc->elevation = (double)(h % 350);
		loc->found = 1;
	}
	return 1;
}

static int fetch_live_weather(const struct location_info *loc, struct weather_data *w)
{
	char path[512];
	char *body;
	const char *cur;
	const char *daily;

	memset(w, 0, sizeof(*w));
	sprintf(path,
		"/v1/forecast?latitude=%.4f&longitude=%.4f"
		"&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
		"precipitation,weather_code,cloud_cover,pressure_msl,wind_speed_10m,"
		"wind_direction_10m,wind_gusts_10m"
		"&daily=weather_code,temperature_2m_max,temperature_2m_min,"
		"precipitation_probability_max,wind_speed_10m_max"
		"&timezone=auto&forecast_days=5",
		loc->lat, loc->lon);

	body = fetch_url("api.open-meteo.com", 443, path, 1);
	if (!body) return 0;

	cur = strstr(body, "\"current\":{");
	if (!cur) return 0;

	json_extract_string(body, "timezone", w->timezone, sizeof(w->timezone));

	{
		double v = 0;
		if (!json_extract_double(cur, "temperature_2m", &w->temp_c)) return 0;
		if (json_extract_double(cur, "apparent_temperature", &v)) w->feels_c = v;
		else w->feels_c = w->temp_c;
		if (json_extract_double(cur, "relative_humidity_2m", &v)) w->humidity = (int)v;
		if (json_extract_double(cur, "is_day", &v)) w->is_day = (int)v;
		else w->is_day = 1;
		if (json_extract_double(cur, "precipitation", &v)) w->precip_mm = v;
		if (json_extract_double(cur, "weather_code", &v)) w->weather_code = (int)v;
		if (json_extract_double(cur, "cloud_cover", &v)) w->cloud_cover = (int)v;
		if (json_extract_double(cur, "pressure_msl", &v)) w->pressure_hpa = v;
		else w->pressure_hpa = 1013.2;
		if (json_extract_double(cur, "wind_speed_10m", &v)) w->wind_kmh = v;
		if (json_extract_double(cur, "wind_direction_10m", &v)) w->wind_dir_deg = (int)v;
		if (json_extract_double(cur, "wind_gusts_10m", &v)) w->wind_gusts_kmh = v;
	}

	daily = strstr(cur, "\"daily\":{");
	if (daily) {
		double codes[5], tmax[5], tmin[5], pprob[5], wmax[5];
		int n_dates;
		int i;
		char dates[5][16];
		n_dates = json_extract_array_strings(daily, "time", dates, 5);
		json_extract_array_doubles(daily, "weather_code", codes, 5);
		json_extract_array_doubles(daily, "temperature_2m_max", tmax, 5);
		json_extract_array_doubles(daily, "temperature_2m_min", tmin, 5);
		json_extract_array_doubles(daily, "precipitation_probability_max", pprob, 5);
		json_extract_array_doubles(daily, "wind_speed_10m_max", wmax, 5);

		w->num_days = n_dates;
		for (i = 0; i < n_dates && i < 5; i++) {
			strcpy(w->days[i].date, dates[i]);
			w->days[i].weather_code = (int)codes[i];
			w->days[i].temp_max_c = tmax[i];
			w->days[i].temp_min_c = tmin[i];
			w->days[i].precip_prob = (int)pprob[i];
			w->days[i].wind_max_kmh = wmax[i];
		}
	}
	w->is_offline = 0;
	return 1;
}

static void generate_offline_weather(const struct location_info *loc, struct weather_data *w)
{
	int i;
	double base_c = 20.5;
	unsigned int seed = 0;
	const char *p = loc->name;
	time_t now = time(NULL);
	struct tm *tm_now = localtime(&now);
	static const int sim_codes[] = { 0, 1, 2, 2, 0, 3, 61, 1 };

	memset(w, 0, sizeof(*w));
	while (*p) seed = seed * 31 + (unsigned char)tolower((unsigned char)*p++);

	for (i = 0; builtin_cities[i].name != NULL; i++) {
		if (str_case_cmp(loc->name, builtin_cities[i].name) == 0) {
			base_c = builtin_cities[i].base_temp_c;
			break;
		}
	}

	if (tm_now) {
		int hr = tm_now->tm_hour;
		double diurnal = (hr >= 6 && hr <= 18) ? 2.5 : -2.0;
		w->is_day = (hr >= 6 && hr < 19) ? 1 : 0;
		w->temp_c = base_c + diurnal + (double)((seed % 5) - 2) * 0.4;
		seed += (unsigned int)(tm_now->tm_yday * 7);
	} else {
		w->is_day = 1;
		w->temp_c = base_c;
	}

	w->feels_c = w->temp_c - 0.6;
	w->weather_code = sim_codes[seed % 8];
	w->humidity = 48 + (int)(seed % 35);
	w->cloud_cover = (w->weather_code == 0) ? 5 : (w->weather_code == 1 ? 20 : (w->weather_code == 2 ? 45 : 80));
	w->precip_mm = (w->weather_code >= 51) ? 1.8 : 0.0;
	w->pressure_hpa = 1012.0 + (double)((seed % 15) - 7);
	w->wind_kmh = 8.5 + (double)(seed % 18);
	w->wind_gusts_kmh = w->wind_kmh + 6.5;
	w->wind_dir_deg = (int)((seed * 45) % 360);
	strcpy(w->timezone, "Local Climatology");

	w->num_days = 5;
	for (i = 0; i < 5; i++) {
		time_t day_t = now + (time_t)(i * 86400);
		struct tm *dtm = localtime(&day_t);
		unsigned int dseed = seed + (unsigned int)(i * 13);
		if (dtm) {
			sprintf(w->days[i].date, "%04d-%02d-%02d",
				dtm->tm_year + 1900, dtm->tm_mon + 1, dtm->tm_mday);
		} else {
			sprintf(w->days[i].date, "2026-09-%02d", 21 + i);
		}
		w->days[i].weather_code = sim_codes[dseed % 8];
		w->days[i].temp_max_c = base_c + 3.2 + (double)((dseed % 5) - 2);
		w->days[i].temp_min_c = base_c - 5.4 + (double)((dseed % 3) - 1);
		w->days[i].precip_prob = (w->days[i].weather_code >= 51) ? 65 : ((dseed % 4) * 10);
		w->days[i].wind_max_kmh = 12.0 + (double)(dseed % 16);
	}
	w->is_offline = 1;
}

static void render_retro_wunderground(const struct location_info *loc, const struct weather_data *w)
{
	int i;
	printf("==============================================================================\n");
	printf("        UNIVERSITY OF MICHIGAN WEATHER UNDERGROUND (rainmaker.wunderground)   \n");
	printf("                     SIX / Linux 2.0.11 National Weather Service              \n");
	printf("==============================================================================\n\n");
	printf(" Station : %s%s%s, %s\n",
	       loc->name,
	       loc->admin1[0] ? ", " : "",
	       loc->admin1,
	       loc->country);
	printf(" Coords  : %.2f N, %.2f E   (Elevation: %.0f m / %.0f ft)   Source: %s\n",
	       loc->lat, loc->lon, loc->elevation, loc->elevation * 3.28084,
	       w->is_offline ? "Climatology DB (Offline)" : "Open-Meteo Live Feed");
	printf("------------------------------------------------------------------------------\n");
	printf(" CURRENT OBSERVATIONS:\n");
	printf("   Weather     : %s\n", wmo_description(w->weather_code));
	printf("   Temperature : %.1f F  (%.1f C)\n", c_to_f(w->temp_c), w->temp_c);
	printf("   Feels Like  : %.1f F  (%.1f C)\n", c_to_f(w->feels_c), w->feels_c);
	printf("   Humidity    : %d %%\n", w->humidity);
	printf("   Winds       : %s at %.1f mph (%.1f km/h), Gusts to %.1f mph\n",
	       wind_dir_compass(w->wind_dir_deg),
	       kmh_to_mph(w->wind_kmh), w->wind_kmh, kmh_to_mph(w->wind_gusts_kmh));
	printf("   Barometer   : %.1f hPa (%.2f inHg)\n",
	       w->pressure_hpa, w->pressure_hpa * 0.02953);
	printf("------------------------------------------------------------------------------\n");
	printf(" 5-DAY EXTENDED OUTLOOK:\n");
	printf("   DAY   DATE         CONDITIONS            HIGH / LOW (F)     HIGH / LOW (C)  POP\n");
	printf("   ---   ----------   -------------------   ---------------    --------------  ---\n");
	for (i = 0; i < w->num_days; i++) {
		printf("   %-3s   %-10s   %-19.19s   %5.1f / %5.1f F    %5.1f / %5.1f C  %2d%%\n",
		       weekday_from_date(w->days[i].date),
		       w->days[i].date,
		       wmo_description(w->days[i].weather_code),
		       c_to_f(w->days[i].temp_max_c), c_to_f(w->days[i].temp_min_c),
		       w->days[i].temp_max_c, w->days[i].temp_min_c,
		       w->days[i].precip_prob);
	}
	printf("==============================================================================\n");
}

static void render_ansi_dashboard(const struct location_info *loc, const struct weather_data *w, int unit_pref)
{
	const char *art[5];
	const char *art_color = "";
	const char *c_reset  = use_color ? "\033[0m" : "";
	const char *c_bold   = use_color ? "\033[1m" : "";
	const char *c_cyan   = use_color ? "\033[1;36m" : "";
	const char *c_yellow = use_color ? "\033[1;33m" : "";
	const char *c_green  = use_color ? "\033[1;32m" : "";
	const char *c_dim    = use_color ? "\033[0;37m" : "";
	char temp_str[64];
	char feels_str[64];
	char wind_str[64];
	char elev_str[32];
	char hum_str[64];
	char pres_str[64];
	char loc_str[196];
	int i;

	get_ascii_art(w->weather_code, w->is_day, art, &art_color);
	if (!use_color) art_color = "";

	if (unit_pref == 'F') {
		sprintf(temp_str, "%.1f F (%.1f C)", c_to_f(w->temp_c), w->temp_c);
		sprintf(feels_str, "%.1f F (%.1f C)", c_to_f(w->feels_c), w->feels_c);
		sprintf(wind_str, "%s %.1f mph (%.1f km/h)",
			wind_dir_compass(w->wind_dir_deg), kmh_to_mph(w->wind_kmh), w->wind_kmh);
		sprintf(elev_str, "Elev: %4.0fft", loc->elevation * 3.28084);
		sprintf(pres_str, "%.2f inHg  Precip: %.2f in",
			w->pressure_hpa * 0.02953, w->precip_mm * 0.03937);
	} else {
		sprintf(temp_str, "%.1f C (%.1f F)", w->temp_c, c_to_f(w->temp_c));
		sprintf(feels_str, "%.1f C (%.1f F)", w->feels_c, c_to_f(w->feels_c));
		sprintf(wind_str, "%s %.1f km/h (%.1f mph)",
			wind_dir_compass(w->wind_dir_deg), w->wind_kmh, kmh_to_mph(w->wind_kmh));
		sprintf(elev_str, "Elev: %4.0fm", loc->elevation);
		sprintf(pres_str, "%.1f hPa   Precip: %.1f mm", w->pressure_hpa, w->precip_mm);
	}
	sprintf(hum_str, "%d%%   Cloud Cover: %d%%", w->humidity, w->cloud_cover);

	sprintf(loc_str, "%s%s%s, %s",
		loc->name,
		loc->admin1[0] ? ", " : "",
		loc->admin1,
		loc->country);

	printf("%s+----------------------------------------------------------------------------+%s\n", c_cyan, c_reset);
	printf("%s|%s %sSIX WEATHER REPORT%s  -  %s%-51.51s%s %s|\n",
	       c_cyan, c_reset,
	       c_yellow, c_reset,
	       c_bold, loc_str, c_reset, c_cyan);
	printf("%s+----------------------------------------------------------------------------+%s\n", c_cyan, c_reset);

	printf("%s|%s  %s%s%s   %-16.16s  %sTemp      :%s %s%-27s%s %s|\n",
	       c_cyan, c_reset, art_color, art[0], c_reset,
	       wmo_description(w->weather_code),
	       c_dim, c_reset, c_green, temp_str, c_reset, c_cyan);

	printf("%s|%s  %s%s%s   Lat: %6.2f       %sFeels Like:%s %-27s %s|\n",
	       c_cyan, c_reset, art_color, art[1], c_reset,
	       loc->lat,
	       c_dim, c_reset, feels_str, c_cyan);

	printf("%s|%s  %s%s%s   Lon: %6.2f       %sWind      :%s %-27s %s|\n",
	       c_cyan, c_reset, art_color, art[2], c_reset,
	       loc->lon,
	       c_dim, c_reset, wind_str, c_cyan);

	printf("%s|%s  %s%s%s   %-16.16s  %sHumidity  :%s %-27s %s|\n",
	       c_cyan, c_reset, art_color, art[3], c_reset,
	       elev_str,
	       c_dim, c_reset, hum_str, c_cyan);

	printf("%s|%s  %s%s%s   %-16.16s  %sPressure  :%s %-27s %s|\n",
	       c_cyan, c_reset, art_color, art[4], c_reset,
	       w->is_offline ? "[Offline Mode]" : "[Live Feed]",
	       c_dim, c_reset, pres_str, c_cyan);

	printf("%s+----------------------------------------------------------------------------+%s\n", c_cyan, c_reset);
	printf("%s|%s %s5-DAY FORECAST%s                                                             %s|\n",
	       c_cyan, c_reset, c_yellow, c_reset, c_cyan);
	printf("%s+--------------+----------------+----------------------+----------+----------+%s\n", c_cyan, c_reset);
	printf("%s|%s %sDay / Date%s   %s|%s %sCondition%s      %s|%s %sHigh / Low%s           %s|%s %sRain %% %s  %s|%s %sMax Wind%s %s|\n",
	       c_cyan, c_reset, c_bold, c_reset,
	       c_cyan, c_reset, c_bold, c_reset,
	       c_cyan, c_reset, c_bold, c_reset,
	       c_cyan, c_reset, c_bold, c_reset,
	       c_cyan, c_reset, c_bold, c_reset, c_cyan);
	printf("%s+--------------+----------------+----------------------+----------+----------+%s\n", c_cyan, c_reset);

	for (i = 0; i < w->num_days; i++) {
		char hl_str[32];
		char wmax_str[16];
		const char *date_short = (strlen(w->days[i].date) >= 10) ? (w->days[i].date + 5) : w->days[i].date;
		if (unit_pref == 'F') {
			sprintf(hl_str, "%5.1fF / %5.1fF",
				c_to_f(w->days[i].temp_max_c), c_to_f(w->days[i].temp_min_c));
			sprintf(wmax_str, "%4.0f mph", kmh_to_mph(w->days[i].wind_max_kmh));
		} else {
			sprintf(hl_str, "%5.1fC / %5.1fC",
				w->days[i].temp_max_c, w->days[i].temp_min_c);
			sprintf(wmax_str, "%4.0f kmh", w->days[i].wind_max_kmh);
		}
		printf("%s|%s %s%-3s%s %-8s %s|%s %-14.14s %s|%s %-20s %s|%s   %3d%%   %s|%s %-8s %s|\n",
		       c_cyan, c_reset,
		       c_yellow, weekday_from_date(w->days[i].date), c_reset, date_short,
		       c_cyan, c_reset,
		       wmo_short_icon(w->days[i].weather_code),
		       c_cyan, c_reset,
		       hl_str,
		       c_cyan, c_reset,
		       w->days[i].precip_prob,
		       c_cyan, c_reset,
		       wmax_str,
		       c_cyan);
	}
	printf("%s+--------------+----------------+----------------------+----------+----------+%s\n", c_cyan, c_reset);
}

static void usage(void)
{
	printf("Usage: weather [options] [city...]\n\n");
	printf("Options:\n");
	printf("  -m, --metric, -c, --celsius        Metric units (C, km/h, m, hPa, mm) [default]\n");
	printf("  -i, --imperial, -f, --fahrenheit   Imperial units (F, mph, ft, inHg, in)\n");
	printf("  -r, --retro                        1995 Weather Underground teleprinter style\n");
	printf("  -o, --offline                      Force offline climatology mode\n");
	printf("  -n, --no-color                     Disable ANSI terminal colors\n");
	printf("  -h, --help                         Show this help message\n\n");
	printf("Examples:\n");
	printf("  weather                  Auto-detect current location (or Mountain View)\n");
	printf("  weather Bangalore        Current weather & 5-day forecast for Bangalore\n");
	printf("  weather -i \"New York\"    Forecast in Imperial units (F, mph, inHg)\n");
	printf("  weather -r Tokyo         1995 Weather Underground teleprinter mode\n");
}

int main(int argc, char **argv)
{
	char query[128];
	int unit_pref = 'C';
	int retro_mode = 0;
	int force_offline = 0;
	int i;
	struct location_info loc;
	struct weather_data w;

	query[0] = '\0';

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage();
			return 0;
		} else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--metric") == 0 ||
			   strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--celsius") == 0) {
			unit_pref = 'C';
		} else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--imperial") == 0 ||
			   strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fahrenheit") == 0) {
			unit_pref = 'F';
		} else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--retro") == 0) {
			retro_mode = 1;
		} else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--offline") == 0) {
			force_offline = 1;
		} else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--no-color") == 0) {
			use_color = 0;
		} else {
			if (query[0] != '\0' && strlen(query) + 2 < sizeof(query))
				strcat(query, " ");
			if (strlen(query) + strlen(argv[i]) + 1 < sizeof(query))
				strcat(query, argv[i]);
		}
	}

	resolve_location(query, force_offline, &loc);
	if (force_offline || !fetch_live_weather(&loc, &w)) {
		generate_offline_weather(&loc, &w);
	}

	if (retro_mode) {
		render_retro_wunderground(&loc, &w);
	} else {
		render_ansi_dashboard(&loc, &w, unit_pref);
	}

	return 0;
}
