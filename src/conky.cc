/* -*- mode: c++; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: t -*-
 * vim: ts=4 sw=4 noet ai cindent syntax=cpp
 *
 * Conky, a system monitor, based on torsmo
 *
 * Any original torsmo code is licensed under the BSD license
 *
 * All code written since the fork of torsmo is licensed under the GPL
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2004, Hannu Saransaari and Lauri Hakkarainen
 * Copyright (c) 2005-2012 Brenden Matthews, Philip Kovacs, et. al.
 *	(see AUTHORS)
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"
#include "text_object.h"
#include "conky.h"
#include "common.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <string>
#include <stdarg.h>
#include <cmath>
#include <ctime>
#include <locale.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif /* HAVE_DIRENT_H */
#include <sys/time.h>
#include <sys/param.h>
#ifdef HAVE_SYS_INOTIFY_H
#include <sys/inotify.h>
#endif /* HAVE_SYS_INOTIFY_H */
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <getopt.h>
#if defined BUILD_WEATHER_XOAP || defined BUILD_RSS
#include <libxml/parser.h>
#endif
#ifdef BUILD_CURL
#include <curl/curl.h>
#endif

/* local headers */
#include "core.h"
#include "build.h"
#include "colours.h"
#include "diskio.h"
#include "exec.h"
#ifdef BUILD_ICONV
#include "iconv_tools.h"
#endif
#include "llua.h"
#include "logging.h"
#include "mail.h"
#include "nc.h"
#include "net_stat.h"
#include "temphelper.h"
#include "template.h"
#include "timeinfo.h"
#include "top.h"
#ifdef BUILD_MYSQL
#include "mysql.h"
#endif /* BUILD_MYSQL */
#ifdef BUILD_NVIDIA
#include "nvidia.h"
#endif
#ifdef BUILD_CURL
#include "ccurl_thread.h"
#endif /* BUILD_CURL */
#ifdef BUILD_WEATHER_METAR
#include "weather.h"
#endif /* BUILD_WEATHER_METAR */

#include "lua-config.hh"
#include "setting.hh"
#include "output-method.hh"

/* check for OS and include appropriate headers */
#if defined(__linux__)
#include "linux.h"
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include "freebsd.h"
#elif defined(__DragonFly__)
#include "dragonfly.h"
#elif defined(__OpenBSD__)
#include "openbsd.h"
#endif
#ifdef BUILD_HTTP
#include <microhttpd.h>
#endif

#if defined(__FreeBSD_kernel__)
#include <bsd/bsd.h>
#endif

#ifdef BUILD_BUILTIN_CONFIG
#include "defconfig.h"

namespace { const char builtin_config_magic[] = "==builtin=="; }
#endif

#ifdef BUILD_OLD_CONFIG
#include "convertconf.h"
#endif

#ifndef S_ISSOCK
#define S_ISSOCK(x)   ((x & S_IFMT) == S_IFSOCK)
#endif

#define MAX_IF_BLOCK_DEPTH 5

/* debugging level, used by logging.h */
int global_debug_level = 0;

/* disable inotify auto reload feature if desired */
static conky::simple_config_setting<bool> disable_auto_reload("disable_auto_reload", false, false);

enum spacer_state {
	NO_SPACER = 0,
	LEFT_SPACER,
	RIGHT_SPACER
};
template<>
conky::lua_traits<spacer_state>::Map conky::lua_traits<spacer_state>::map = {
	{ "none",  NO_SPACER },
	{ "left",  LEFT_SPACER },
	{ "right", RIGHT_SPACER }
};
static conky::simple_config_setting<spacer_state> use_spacer("use_spacer", NO_SPACER, false);

/* variables holding various config settings */
static conky::simple_config_setting<bool> short_units("short_units", false, true);
static conky::simple_config_setting<bool> format_human_readable("format_human_readable",
																true, true);

static conky::simple_config_setting<bool> out_to_stderr("out_to_stderr", false, false);


int top_cpu, top_mem, top_time;
#ifdef BUILD_IOSTATS
int top_io;
#endif
int top_running;
static conky::simple_config_setting<bool> extra_newline("extra_newline", false, false);
static volatile int g_signal_pending;

/* Update interval */
conky::range_config_setting<double> update_interval("update_interval", 0.0,
										std::numeric_limits<double>::infinity(), 3.0, true);
conky::range_config_setting<double> update_interval_on_battery("update_interval_on_battery", 0.0,
										std::numeric_limits<double>::infinity(), NOBATTERY, true);
static bool on_battery = false;

double active_update_interval()
{ return (on_battery?*update_interval_on_battery:*update_interval); }

const double music_player_interval_setting::set_default(bool)
{
	return set(*update_interval);
}

music_player_interval_setting music_player_interval;

void *global_cpu = NULL;
static conky::range_config_setting<unsigned int> max_text_width("max_text_width", 0,
											std::numeric_limits<unsigned int>::max(), 0, true);

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
extern kvm_t *kd;
#endif

int argc_copy;
char** argv_copy;

/* prototypes for internally used functions */
static void signal_handler(int);
static void reload_config(void);

static void print_version(void)
{
        std::cout << _(PACKAGE_NAME" " VERSION" compiled " BUILD_DATE" for " BUILD_ARCH"\n"
                "\nCompiled in features:\n\n"
                "System config file: " SYSTEM_CONFIG_FILE"\n"
                "Package library path: " PACKAGE_LIBDIR"\n\n")
                << _("\n General:\n")
#ifdef HAVE_OPENMP
                << _("  * OpenMP\n")
#endif /* HAVE_OPENMP */
#ifdef BUILD_MATH
                << _("  * math\n")
#endif /* BUILD_MATH */
#ifdef BUILD_HDDTEMP
                << _("  * hddtemp\n")
#endif /* BUILD_HDDTEMP */
#ifdef BUILD_PORT_MONITORS
                << _("  * portmon\n")
#endif /* BUILD_PORT_MONITORS */
#ifdef BUILD_HTTP
                << _("  * HTTP\n")
#endif
#ifdef BUILD_IPV6
                << _("  * IPv6\n")
#endif /* BUILD_IPV6 */
#ifdef BUILD_IRC
                << _("  * IRC\n")
#endif
#ifdef BUILD_CURL
                << _("  * Curl\n")
#endif /* BUILD_CURL */
#ifdef BUILD_RSS
                << _("  * RSS\n")
#endif /* BUILD_RSS */
#ifdef BUILD_WEATHER_METAR
                << _("  * Weather (METAR)\n")
#ifdef BUILD_WEATHER_XOAP
                << _("  * Weather (XOAP)\n")
#endif /* BUILD_WEATHER_XOAP */
#endif /* BUILD_WEATHER_METAR */
#ifdef BUILD_WLAN
                << _("  * wireless\n")
#endif /* BUILD_WLAN */
#ifdef BUILD_IBM
                << _("  * support for IBM/Lenovo notebooks\n")
#endif /* BUILD_IBM */
#ifdef BUILD_NVIDIA
                << _("  * nvidia\n")
#endif /* BUILD_NVIDIA */
#ifdef BUILD_EVE
                << _("  * eve-online\n")
#endif /* BUILD_EVE */
#ifdef BUILD_BUILTIN_CONFIG
                << _("  * builtin default configuration\n")
#endif /* BUILD_BUILTIN_CONFIG */
#ifdef BUILD_OLD_CONFIG
                << _("  * old configuration syntax\n")
#endif /* BUILD_OLD_CONFIG */
#ifdef BUILD_IMLIB2
                << _("  * Imlib2\n")
#endif /* BUILD_IMLIB2 */
#ifdef BUILD_MIXER_ALSA
                << _("  * ALSA mixer support\n")
#endif /* BUILD_MIXER_ALSA */
#ifdef BUILD_APCUPSD
                << _("  * apcupsd\n")
#endif /* BUILD_APCUPSD */
#ifdef BUILD_IOSTATS
                << _("  * iostats\n")
#endif /* BUILD_IOSTATS */
#ifdef BUILD_NCURSES
                << _("  * ncurses\n")
#endif /* BUILD_NCURSES */
#ifdef BUILD_I18N
                << _("  * Internationalization support\n")
#endif
#ifdef DEBUG
                << _("  * Debugging extensions\n")
#endif
#if defined BUILD_LUA_CAIRO || defined BUILD_LUA_IMLIB2
                << _("\n Lua bindings:\n")
#endif
#ifdef BUILD_LUA_CAIRO
                << _("  * Cairo\n")
#endif /* BUILD_LUA_CAIRO */
#ifdef BUILD_LUA_IMLIB2
                << _("  * Imlib2\n")
#endif /* BUILD_LUA_IMLIB2 */
#ifdef BUILD_X11
                << _(" X11:\n")
# ifdef BUILD_XDBE
                << _("  * XDBE (double buffer extension)\n")
# endif /* BUILD_XDBE */
# ifdef BUILD_XFT
                << _("  * Xft\n")
# endif /* BUILD_XFT */
# ifdef BUILD_ARGB
                << _("  * ARGB visual\n")
# endif /* BUILD_ARGB */
                << _("  * Own window\n")
#endif /* BUILD_X11 */
#if defined BUILD_AUDACIOUS || defined BUILD_BMPX || defined BUILD_CMUS || defined BUILD_MPD || defined BUILD_MOC || defined BUILD_XMMS2
                << _("\n Music detection:\n")
#endif
#ifdef BUILD_AUDACIOUS
                << _("  * Audacious\n")
#endif /* BUILD_AUDACIOUS */
#ifdef BUILD_BMPX
                << _("  * BMPx\n")
#endif /* BUILD_BMPX */
#ifdef BUILD_CMUS
                << _("  * CMUS\n")
#endif /* BUILD_CMUS */
#ifdef BUILD_MPD
                << _("  * MPD\n")
#endif /* BUILD_MPD */
#ifdef BUILD_MOC
                << _("  * MOC\n")
#endif /* BUILD_MOC */
#ifdef BUILD_XMMS2
                << _("  * XMMS2\n")
#endif /* BUILD_XMMS2 */
	<< _("\n Default values:\n")
	<< "  * Netdevice: " DEFAULTNETDEV"\n"
	<< "  * Local configfile: " CONFIG_FILE"\n"
#ifdef BUILD_I18N
	<< "  * Localedir: " LOCALE_DIR"\n"
#endif
#ifdef BUILD_HTTP
	<< "  * HTTP-port: " << HTTPPORT << "\n"
#endif
	<< "  * Maximum netdevices: " << MAX_NET_INTERFACES << "\n"
	<< "  * Maximum text size: " << MAX_USER_TEXT_DEFAULT << "\n"
	<< "  * Size text buffer: " << DEFAULT_TEXT_BUFFER_SIZE << "\n"
        ;
}


static const char *suffixes[] = {
	_nop("B"), _nop("KiB"), _nop("MiB"), _nop("GiB"), _nop("TiB"), _nop("PiB"), ""
};


/* struct that has all info to be shared between
 * instances of the same text object */
struct information info;

/* path to config file */
std::string current_config;

/* set to 1 if you want all text to be in uppercase */
static conky::simple_config_setting<bool> stuff_in_uppercase("uppercase", false, true);

/* Run how many times? */
static conky::range_config_setting<unsigned long> total_run_times("total_run_times", 0,
									std::numeric_limits<unsigned long>::max(), 0, true);

/* fork? */
static conky::simple_config_setting<bool> fork_to_background("background", false, false);

/* set to 0 after the first time conky is run, so we don't fork again after the
 * first forking */
static int first_pass = 1;

conky::range_config_setting<int> cpu_avg_samples("cpu_avg_samples", 1, 14, 2, true);
conky::range_config_setting<int> net_avg_samples("net_avg_samples", 1, 14, 2, true);
conky::range_config_setting<int> diskio_avg_samples("diskio_avg_samples", 1, 14, 2, true);

#if 0 && X11 // XXX
/* filenames for output */
static conky::simple_config_setting<std::string> overwrite_file("overwrite_file",
																std::string(), true);
static FILE *overwrite_fpointer = NULL;
static conky::simple_config_setting<std::string> append_file("append_file",
																std::string(), true);
static FILE *append_fpointer = NULL;
#endif

#ifdef BUILD_HTTP
std::string webpage;
struct MHD_Daemon *httpd;
static conky::simple_config_setting<bool> http_refresh("http_refresh", false, true);

int sendanswer(void *cls, struct MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls) {
	struct MHD_Response *response = MHD_create_response_from_data(webpage.length(), (void*) webpage.c_str(), MHD_NO, MHD_NO);
	int ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
	MHD_destroy_response(response);
	if(cls || url || method || version || upload_data || upload_data_size || con_cls) {}	//make compiler happy
	return ret;
}

class out_to_http_setting: public conky::simple_config_setting<bool> {
	typedef conky::simple_config_setting<bool> Base;

protected:
	virtual void cleanup(lua::state &l)
	{
		lua::stack_sentry s(l, -1);

		if(do_convert(l, -1).first) {
			MHD_stop_daemon(httpd);
			httpd = NULL;
		}

		l.pop();
	}

public:
	virtual const bool set(const bool &r, bool init)
    {
        if(init) {
			if(r) {
				httpd = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, HTTPPORT,
						NULL, NULL, &sendanswer, NULL, MHD_OPTION_END);
			}
			value = r;
        }
		return value;
    }

	out_to_http_setting()
		: Base("out_to_http", false, false)
	{}
};
static out_to_http_setting out_to_http;
#endif

#ifdef BUILD_X11

static conky::simple_config_setting<bool> show_graph_scale("show_graph_scale", false, false);
static conky::simple_config_setting<bool> show_graph_range("show_graph_range", false, false);

/* Position on the screen */

/* border */
static conky::simple_config_setting<bool> draw_borders("draw_borders", false, false);
static conky::simple_config_setting<bool> draw_graph_borders("draw_graph_borders", true, false);

static conky::simple_config_setting<bool> draw_shades("draw_shades", true, false);
static conky::simple_config_setting<bool> draw_outline("draw_outline", false, false);

#ifdef OWN_WINDOW
/* fixed size/pos is set if wm/user changes them */
static int fixed_size = 0, fixed_pos = 0;
#endif

static conky::range_config_setting<int> minimum_height("minimum_height", 0,
											std::numeric_limits<int>::max(), 5, true);
static conky::range_config_setting<int> minimum_width("minimum_width", 0,
											std::numeric_limits<int>::max(), 5, true);
static conky::range_config_setting<int> maximum_width("maximum_width", 0,
											std::numeric_limits<int>::max(), 0, true);

static bool isutf8(const char* envvar) {
	char *s = getenv(envvar);
	if(s) {
		std::string temp = s;
		std::transform(temp.begin(), temp.end(), temp.begin(), ::tolower);
		if( (temp.find("utf-8") != std::string::npos) || (temp.find("utf8") != std::string::npos) ) {
			return true;
		}
	}
	return false;
}

/* UTF-8 */
static conky::simple_config_setting<bool> utf8_mode("override_utf8_locale",
					isutf8("LC_ALL") || isutf8("LC_CTYPE") || isutf8("LANG"), false);

#endif /* BUILD_X11 */

/* maximum size of config TEXT buffer, i.e. below TEXT line. */
conky::range_config_setting<unsigned int> max_user_text("max_user_text", 47,
					std::numeric_limits<unsigned int>::max(), MAX_USER_TEXT_DEFAULT, false);

/* maximum size of individual text buffers, ie $exec buffer size */
conky::range_config_setting<unsigned int> text_buffer_size("text_buffer_size",
				DEFAULT_TEXT_BUFFER_SIZE, std::numeric_limits<unsigned int>::max(),
				DEFAULT_TEXT_BUFFER_SIZE, false);

/* pad percentages to decimals? */
static conky::simple_config_setting<int> pad_percents("pad_percents", 0, false);

static std::shared_ptr<conky::layout_item> global_text;
const std::shared_ptr<conky::layout_item>& get_global_text()
{ return global_text; }


long global_text_lines;

static int total_updates;
static int updatereset;

std::unique_ptr<lua::state> state;

void set_updatereset(int i)
{
	updatereset = i;
}

int get_updatereset(void)
{
	return updatereset;
}

int get_total_updates(void)
{
	return total_updates;
}

/* quite boring functions */

static inline void for_each_line(char *b, int f(char *, int))
{
	char *ps, *pe;
	int special_index = 0; /* specials index */

	if(! b) return;
	for (ps = b, pe = b; *pe; pe++) {
		if (*pe == '\n') {
			*pe = '\0';
			special_index = f(ps, special_index);
			*pe = '\n';
			ps = pe + 1;
		}
	}

	if (ps < pe) {
		f(ps, special_index);
	}
}

/* Prints anything normally printed with snprintf according to the current value
 * of use_spacer.  Actually slightly more flexible than snprintf, as you can
 * safely specify the destination buffer as one of your inputs.  */
int spaced_print(char *buf, int size, const char *format, int width, ...)
{
	int len = 0;
	va_list argp;
	char *tempbuf;

	if (size < 1) {
		return 0;
	}
	tempbuf = (char*)malloc(size * sizeof(char));

	// Passes the varargs along to vsnprintf
	va_start(argp, width);
	vsnprintf(tempbuf, size, format, argp);
	va_end(argp);

	switch (*use_spacer) {
		case NO_SPACER:
			len = snprintf(buf, size, "%s", tempbuf);
			break;
		case LEFT_SPACER:
			len = snprintf(buf, size, "%*s", width, tempbuf);
			break;
		case RIGHT_SPACER:
			len = snprintf(buf, size, "%-*s", width, tempbuf);
			break;
	}
	free(tempbuf);
	return len;
}

/* print percentage values
 *
 * - i.e., unsigned values between 0 and 100
 * - respect the value of pad_percents */
int percent_print(char *buf, int size, unsigned value)
{
	return spaced_print(buf, size, "%u", *pad_percents, value);
}

#if defined(__FreeBSD__)
unsigned long long llabs(long long num) {
       if(num < 0) return -num;
       else return num;
}
#endif

/* converts from bytes to human readable format (K, M, G, T)
 *
 * The algorithm always divides by 1024, as unit-conversion of byte
 * counts suggests. But for output length determination we need to
 * compare with 1000 here, as we print in decimal form. */
void human_readable(long long num, char *buf, int size)
{
	const char **suffix = suffixes;
	float fnum;
	int precision;
	int width;
	const char *format;

	/* Possibly just output as usual, for example for stdout usage */
	if (not *format_human_readable) {
		spaced_print(buf, size, "%d", 6, round_to_int(num));
		return;
	}
	if (*short_units) {
		width = 5;
		format = "%.*f%.1s";
	} else {
		width = 7;
		format = "%.*f%-3s";
	}

	if (llabs(num) < 1000LL) {
		spaced_print(buf, size, format, width, 0, (float)num, _(*suffix));
		return;
	}

	while (llabs(num / 1024) >= 1000LL && **(suffix + 2)) {
		num /= 1024;
		suffix++;
	}

	suffix++;
	fnum = num / 1024.0;

	/* fnum should now be < 1000, so looks like 'AAA.BBBBB'
	 *
	 * The goal is to always have a significance of 3, by
	 * adjusting the decimal part of the number. Sample output:
	 *  123MiB
	 * 23.4GiB
	 * 5.12B
	 * so the point of alignment resides between number and unit. The
	 * upside of this is that there is minimal padding necessary, though
	 * there should be a way to make alignment take place at the decimal
	 * dot (then with fixed width decimal part).
	 *
	 * Note the repdigits below: when given a precision value, printf()
	 * rounds the float to it, not just cuts off the remaining digits. So
	 * e.g. 99.95 with a precision of 1 gets 100.0, which again should be
	 * printed with a precision of 0. Yay. */

	precision = 0;		/* print 100-999 without decimal part */
	if (fnum < 99.95)
		precision = 1;	/* print 10-99 with one decimal place */
	if (fnum < 9.995)
		precision = 2;	/* print 0-9 with two decimal places */

	spaced_print(buf, size, format, width, precision, fnum, _(*suffix));
}

/* global object list root element */
static struct text_object global_root_object;

static long current_text_color;

void set_current_text_color(long colour)
{
	current_text_color = colour;
}

long get_current_text_color(void)
{
	return current_text_color;
}

void parse_conky_vars(struct text_object *root, const char *txt,
		char *p, int p_max_size)
{
	extract_variable_text_internal(root, txt);
	generate_text_internal(p, p_max_size, *root);
}

/* IFBLOCK jumping algorithm
 *
 * This is easier as it looks like:
 * - each IF checks it's condition
 *   - on FALSE: jump
 *   - on TRUE: don't care
 * - each ELSE jumps unconditionally
 * - each ENDIF is silently being ignored
 *
 * Why this works (or: how jumping works):
 * Jumping means to overwrite the "obj" variable of the loop and set it to the
 * target (i.e. the corresponding ELSE or ENDIF). After that, the for-loop does
 * the rest: as regularly, "obj" is being updated to point to obj->next, so
 * object parsing continues right after the corresponding ELSE or ENDIF. This
 * means that if we find an ELSE, it's corresponding IF must not have jumped,
 * so we need to jump always. If we encounter an ENDIF, it's corresponding IF
 * or ELSE has not jumped, and there is nothing to do.
 */
void generate_text_internal(char *p, int p_max_size, struct text_object root)
{
	struct text_object *obj;
	size_t a;

	if(! p) return;

#ifdef BUILD_ICONV
	char *buff_in;

	buff_in = (char *)malloc(p_max_size);
	memset(buff_in, 0, p_max_size);
#endif /* BUILD_ICONV */

	p[0] = 0;
	obj = root.next;
	while (obj && p_max_size > 0) {

		/* check callbacks for existence and act accordingly */
		if (obj->callbacks.print) {
			(*obj->callbacks.print)(obj, p, p_max_size);
		} else if (obj->callbacks.iftest) {
			if (!(*obj->callbacks.iftest)(obj)) {
				DBGP2("jumping");
				if (obj->ifblock_next)
					obj = obj->ifblock_next;
			}
		} else if (obj->callbacks.barval) {
			new_bar(obj, p, p_max_size, (*obj->callbacks.barval)(obj));
		} else if (obj->callbacks.gaugeval) {
			new_gauge(obj, p, p_max_size, (*obj->callbacks.gaugeval)(obj));
#ifdef BUILD_X11
		} else if (obj->callbacks.graphval) {
			new_graph(obj, p, p_max_size, (*obj->callbacks.graphval)(obj));
#endif /* BUILD_X11 */
		} else if (obj->callbacks.percentage) {
			percent_print(p, p_max_size, (*obj->callbacks.percentage)(obj));
		}

		a = strlen(p);
#ifdef BUILD_ICONV
		iconv_convert(&a, buff_in, p, p_max_size);
#endif /* BUILD_ICONV */
		p += a;
		p_max_size -= a;
		(*p) = 0;

		obj = obj->next;
	}
#ifdef BUILD_ICONV
	free(buff_in);
#endif /* BUILD_ICONV */
}

void evaluate(const char *text, char *p, int p_max_size)
{
	struct text_object subroot;

	parse_conky_vars(&subroot, text, p, p_max_size);
	DBGP2("evaluated '%s' to '%s'", text, p);

	free_text_objects(&subroot);
}

double current_update_time, next_update_time, last_update_time;

#if 0 && X11 // XXX
static void generate_text(void)
{
	char *p;
	unsigned int i, j, k;

	special_count = 0;

	/* update info */

	current_update_time = get_time();

	update_stuff();

	/* add things to the buffer */

	/* generate text */

	p = text_buffer;

	generate_text_internal(p, *max_user_text, global_root_object);
	unsigned int mw = *max_text_width;
	unsigned int tbs = *text_buffer_size;
	if(mw > 0) {
		for(i = 0, j = 0; p[i] != 0; i++) {
			if(p[i] == '\n') j = 0;
			else if(j == mw) {
				k = i + strlen(p + i) + 1;
				if(k < tbs) {
					while(k != i) {
						p[k] = p[k-1];
						k--;
					}
					p[k] = '\n';
					j = 0;
				} else NORM_ERR("The end of the text_buffer is reached, increase \"text_buffer_size\"");
			} else j++;
		}
	}

	if (*stuff_in_uppercase) {
		char *tmp_p;

		tmp_p = text_buffer;
		while (*tmp_p) {
			*tmp_p = toupper(*tmp_p);
			tmp_p++;
		}
	}

	double ui = active_update_interval();
	next_update_time += ui;
	if (next_update_time < get_time()) {
		next_update_time = get_time() + ui;
	} else if (next_update_time > get_time() + ui) {
		next_update_time = get_time() + ui;
	}
	last_update_time = current_update_time;
	total_updates++;
}
#endif

#if 0 && BUILD_X11
/* drawing stuff */

static int cur_x, cur_y;	/* current x and y for drawing */
static int draw_mode;		/* FG, BG or OUTLINE */
static long current_color;

static int text_size_updater(char *s, int special_index)
{
	int w = 0;
	char *p;
	special_t *current = specials;

	for(int i = 0; i < special_index; i++)
		current = current->next;

	if (not *out_to_x)
		return 0;
	/* get string widths and skip specials */
	p = s;
	while (*p) {
		if (*p == SPECIAL_CHAR) {
			*p = '\0';
			w += get_string_width(s);
			*p = SPECIAL_CHAR;

			if (current->type == BAR
					|| current->type == GAUGE
					|| current->type == GRAPH) {
				w += current->width;
				if (current->height > last_font_height) {
					last_font_height = current->height;
					last_font_height += font_height();
				}
			} else if (current->type == OFFSET) {
				if (current->arg > 0) {
					w += current->arg;
				}
			} else if (current->type == VOFFSET) {
				last_font_height += current->arg;
			} else if (current->type == GOTO) {
				if (current->arg > cur_x) {
					w = (int) current->arg;
				}
			} else if (current->type == TAB) {
				int start = current->arg;
				int step = current->width;

				if (!step || step < 0) {
					step = 10;
				}
				w += step - (cur_x - text_start_x - start) % step;
			} else if (current->type == FONT) {
				selected_font = current->font_added;
				if (font_height() > last_font_height) {
					last_font_height = font_height();
				}
			}

			special_index++;
			current = current->next;
			s = p + 1;
		}
		p++;
	}

	w += get_string_width(s);

	if (w > text_width) {
		text_width = w;
	}
	int mw = *maximum_width;
	if (text_width > mw && mw > 0) {
		text_width = mw;
	}

	text_height += last_font_height;
	last_font_height = font_height();
	return special_index;
}
#endif /* BUILD_X11 */

static inline void set_foreground_color(long c)
{
#ifdef BUILD_NCURSES
	if (*out_to_ncurses) {
		attron(COLOR_PAIR(c));
	}
#endif /* BUILD_NCURSES */
	UNUSED(c);
	return;
}

std::string string_replace_all(std::string original, std::string oldpart, std::string newpart, std::string::size_type start) {
	std::string::size_type i = start;
	int oldpartlen = oldpart.length();
	while(1) {
		i = original.find(oldpart, i);
		if(i == std::string::npos) break;
		original.replace(i, oldpartlen, newpart);
	}
	return original;
}

#if 0

static void draw_string(const char *s)
{
	int i, i2, pos, width_of_s;
	int max = 0;
	int added;

	if (s[0] == '\0') {
		return;
	}

	width_of_s = get_string_width(s);
	if (*out_to_stdout && draw_mode == FG) {
		printf("%s\n", s);
		if (*extra_newline) fputc('\n', stdout);
		fflush(stdout);	/* output immediately, don't buffer */
	}
	if (*out_to_stderr && draw_mode == FG) {
		fprintf(stderr, "%s\n", s);
		fflush(stderr);	/* output immediately, don't buffer */
	}
	if (draw_mode == FG && overwrite_fpointer) {
		fprintf(overwrite_fpointer, "%s\n", s);
	}
	if (draw_mode == FG && append_fpointer) {
		fprintf(append_fpointer, "%s\n", s);
	}
#ifdef BUILD_NCURSES
	if (*out_to_ncurses && draw_mode == FG) {
		printw("%s", s);
	}
#endif
#ifdef BUILD_HTTP
	if (*out_to_http && draw_mode == FG) {
		std::string::size_type origlen = webpage.length();
		webpage.append(s);
		webpage = string_replace_all(webpage, "\n", "<br />", origlen);
		webpage = string_replace_all(webpage, "  ", "&nbsp;&nbsp;", origlen);
		webpage = string_replace_all(webpage, "&nbsp; ", "&nbsp;&nbsp;", origlen);
		webpage.append("<br />");
	}
#endif
	int tbs = *text_buffer_size;
	memset(tmpstring1, 0, tbs);
	memset(tmpstring2, 0, tbs);
	strncpy(tmpstring1, s, tbs - 1);
	pos = 0;
	added = 0;

	/* This code looks for tabs in the text and coverts them to spaces.
	 * The trick is getting the correct number of spaces, and not going
	 * over the window's size without forcing the window larger. */
	for (i = 0; i < tbs; i++) {
		if (tmpstring1[i] == '\t') {
			i2 = 0;
			for (i2 = 0; i2 < (8 - (1 + pos) % 8) && added <= max; i2++) {
				/* guard against overrun */
				tmpstring2[MIN(pos + i2, tbs - 1)] = ' ';
				added++;
			}
			pos += i2;
		} else {
			/* guard against overrun */
			tmpstring2[MIN(pos, tbs - 1)] = tmpstring1[i];
			pos++;
		}
	}
#ifdef BUILD_X11
	if (*out_to_x) {
		int mw = *maximum_width;
		if (text_width == mw) {
			/* this means the text is probably pushing the limit,
			 * so we'll chop it */
			while (cur_x + get_string_width(tmpstring2) - text_start_x > mw
					&& strlen(tmpstring2) > 0) {
				tmpstring2[strlen(tmpstring2) - 1] = '\0';
			}
		}
	}
#endif /* BUILD_X11 */
	s = tmpstring2;
	memcpy(tmpstring1, s, tbs);
}

int draw_each_line_inner(char *s, int special_index, int last_special_applied)
{
#ifdef BUILD_X11
	int font_h = 0;
	int cur_y_add = 0;
	int mw = *maximum_width;
#endif /* BUILD_X11 */
	char *p = s;
	int last_special_needed = -1;
	int orig_special_index = special_index;

	while (*p) {
		if (*p == SPECIAL_CHAR || last_special_applied > -1) {
#ifdef BUILD_X11
			int w = 0;
#endif /* BUILD_X11 */

			/* draw string before special, unless we're dealing multiline
			 * specials */
			if (last_special_applied > -1) {
				special_index = last_special_applied;
			} else {
				*p = '\0';
				draw_string(s);
				*p = SPECIAL_CHAR;
				s = p + 1;
			}
			/* draw special */
			special_t *current = specials;
			for(int i = 0; i < special_index; i++)
				current = current->next;
			switch (current->type) {
#ifdef BUILD_X11
				case HORIZONTAL_LINE:
				{
					int h = current->height;
					int mid = font_ascent() / 2;

					w = text_start_x + text_width - cur_x;

					XSetLineAttributes(display, window.gc, h, LineSolid,
						CapButt, JoinMiter);
					XDrawLine(display, window.drawable, window.gc, cur_x,
						cur_y - mid / 2, cur_x + w, cur_y - mid / 2);
					break;
				}

				case STIPPLED_HR:
				{
					int h = current->height;
					char tmp_s = current->arg;
					int mid = font_ascent() / 2;
					char ss[2] = { tmp_s, tmp_s };

					w = text_start_x + text_width - cur_x - 1;
					XSetLineAttributes(display, window.gc, h, LineOnOffDash,
						CapButt, JoinMiter);
					XSetDashes(display, window.gc, 0, ss, 2);
					XDrawLine(display, window.drawable, window.gc, cur_x,
						cur_y - mid / 2, cur_x + w, cur_y - mid / 2);
					break;
				}

				case BAR:
				{
					int h, by;
					double bar_usage, scale;
					if (cur_x - text_start_x > mw && mw > 0) {
						break;
					}
					h = current->height;
					bar_usage = current->arg;
					scale = current->scale;
					by = cur_y - (font_ascent() / 2) - 1;

					if (h < font_h) {
						by -= h / 2 - 1;
					}
					w = current->width;
					if (w == 0) {
						w = text_start_x + text_width - cur_x - 1;
					}
					if (w < 0) {
						w = 0;
					}

					XSetLineAttributes(display, window.gc, 1, LineSolid,
						CapButt, JoinMiter);

					XDrawRectangle(display, window.drawable, window.gc, cur_x,
						by, w, h);
					XFillRectangle(display, window.drawable, window.gc, cur_x,
						by, w * bar_usage / scale, h);
					if (h > cur_y_add
							&& h > font_h) {
						cur_y_add = h;
					}
					break;
				}

				case GAUGE: /* new GAUGE  */
				{
					int h, by = 0;
					unsigned long last_colour = current_color;
#ifdef MATH
					float angle, px, py;
					double usage, scale;
#endif /* MATH */

					if (cur_x - text_start_x > mw && mw > 0) {
						break;
					}

					h = current->height;
					by = cur_y - (font_ascent() / 2) - 1;

					if (h < font_h) {
						by -= h / 2 - 1;
					}
					w = current->width;
					if (w == 0) {
						w = text_start_x + text_width - cur_x - 1;
					}
					if (w < 0) {
						w = 0;
					}

					XSetLineAttributes(display, window.gc, 1, LineSolid,
							CapButt, JoinMiter);

					XDrawArc(display, window.drawable, window.gc,
							cur_x, by, w, h * 2, 0, 180*64);

#ifdef MATH
					usage = current->arg;
					scale = current->scale;
					angle = M_PI * usage / scale;
					px = (float)(cur_x+(w/2.))-(float)(w/2.)*cos(angle);
					py = (float)(by+(h))-(float)(h)*sin(angle);

					XDrawLine(display, window.drawable, window.gc,
							cur_x + (w/2.), by+(h), (int)(px), (int)(py));
#endif /* MATH */

					if (h > cur_y_add
							&& h > font_h) {
						cur_y_add = h;
					}

					set_foreground_color(last_colour);

					break;

				}

				case GRAPH:
				{
					int h, by, i = 0, j = 0;
					int colour_idx = 0;
					unsigned long last_colour = current_color;
					if (cur_x - text_start_x > mw && mw > 0) {
						break;
					}
					h = current->height;
					by = cur_y - (font_ascent() / 2) - 1;

					if (h < font_h) {
						by -= h / 2 - 1;
					}
					w = current->width;
					if (w == 0) {
						w = text_start_x + text_width - cur_x - 1;
						current->graph_width = MAX(w - 1, 0);
						if (current->graph_width != current->graph_allocated) {
							w = current->graph_allocated + 1;
						}

					}
					if (w < 0) {
						w = 0;
					}
					if (*draw_graph_borders) {
						XSetLineAttributes(display, window.gc, 1, LineSolid,
							CapButt, JoinMiter);
						XDrawRectangle(display, window.drawable, window.gc,
							cur_x, by, w, h);
					}
					XSetLineAttributes(display, window.gc, 1, LineSolid,
						CapButt, JoinMiter);

					/* in case we don't have a graph yet */
					if (current->graph) {
						unsigned long *tmpcolour = 0;

						if (current->last_colour != 0 || current->first_colour != 0) {
							tmpcolour = do_gradient(w - 1,
									current->last_colour, current->first_colour);
						}
						colour_idx = 0;
						for (i = w - 2; i > -1; i--) {
							if (current->last_colour != 0 || current->first_colour != 0) {
								if (current->tempgrad) {
#ifdef DEBUG_lol
									assert(
											(int)((float)(w - 2) - current->graph[j] *
												(w - 2) / (float)current->scale)
											< w-1
										  );
									assert(
											(int)((float)(w - 2) - current->graph[j] *
												(w - 2) / (float)current->scale)
											> -1
										  );
									if (current->graph[j] == current->scale) {
										assert(
												(int)((float)(w - 2) - current->graph[j] *
													(w - 2) / (float)current->scale)
												== 0
											  );
									}
#endif /* DEBUG_lol */
									set_foreground_color(tmpcolour[
											(int)((float)(w - 2) -
												current->graph[j] * (w - 2) /
												std::max((float)current->scale, 1.0f))
											]);
								} else {
									set_foreground_color(tmpcolour[colour_idx++]);
								}
							}
							/* this is mugfugly, but it works */
							XDrawLine(display, window.drawable, window.gc,
									cur_x + i + 1, by + h, cur_x + i + 1,
									round_to_int((double)by + h - current->graph[j] *
										(h - 1) / current->scale));
							++j;
						}
						free_and_zero(tmpcolour);
					}
					if (h > cur_y_add
							&& h > font_h) {
						cur_y_add = h;
					}
					if (*show_graph_range) {
						int tmp_x = cur_x;
						int tmp_y = cur_y;
						unsigned short int seconds = active_update_interval() * w;
						char *tmp_day_str;
						char *tmp_hour_str;
						char *tmp_min_str;
						char *tmp_sec_str;
						char *tmp_str;
						unsigned short int timeunits;
						if (seconds != 0) {
							timeunits = seconds / 86400; seconds %= 86400;
							if (timeunits <= 0 ||
									asprintf(&tmp_day_str, _("%dd"), timeunits) == -1) {
								tmp_day_str = strdup("");
							}
							timeunits = seconds / 3600; seconds %= 3600;
							if (timeunits <= 0 ||
									asprintf(&tmp_hour_str, _("%dh"), timeunits) == -1) {
								tmp_hour_str = strdup("");
							}
							timeunits = seconds / 60; seconds %= 60;
							if (timeunits <= 0 ||
									asprintf(&tmp_min_str, _("%dm"), timeunits) == -1) {
								tmp_min_str = strdup("");
							}
							if (seconds <= 0 ||
									asprintf(&tmp_sec_str, _("%ds"), seconds) == -1) {
								tmp_sec_str = strdup("");
							}
							if (asprintf(&tmp_str, "%s%s%s%s", tmp_day_str, tmp_hour_str, tmp_min_str, tmp_sec_str) == -1)
								tmp_str = strdup("");
							free(tmp_day_str); free(tmp_hour_str); free(tmp_min_str); free(tmp_sec_str);
						} else {
							tmp_str = strdup(_("Range not possible")); // should never happen, but better safe then sorry
						}
						cur_x += (w / 2) - (font_ascent() * (strlen(tmp_str) / 2));
						cur_y += font_h / 2;
						draw_string(tmp_str);
						free(tmp_str);
						cur_x = tmp_x;
						cur_y = tmp_y;
					}
#ifdef MATH
					if (*show_graph_scale && (current->show_scale == 1)) {
						int tmp_x = cur_x;
						int tmp_y = cur_y;
						char *tmp_str;
						cur_x += font_ascent() / 2;
						cur_y += font_h / 2;
						tmp_str = (char *)
							calloc(log10(floor(current->scale)) + 4,
									sizeof(char));
						sprintf(tmp_str, "%.1f", current->scale);
						draw_string(tmp_str);
						free(tmp_str);
						cur_x = tmp_x;
						cur_y = tmp_y;
					}
#endif
					set_foreground_color(last_colour);
					break;
				}

				case FONT:
				{
					int old = font_ascent();

					cur_y -= font_ascent();
					selected_font = current->font_added;
					set_font();
					if (cur_y + font_ascent() < cur_y + old) {
						cur_y += old;
					} else {
						cur_y += font_ascent();
					}
					font_h = font_height();
					break;
				}
#endif /* BUILD_X11 */
				case FG:
					if (draw_mode == FG) {
						set_foreground_color(current->arg);
					}
					break;

#ifdef BUILD_X11
				case BG:
					if (draw_mode == BG) {
						set_foreground_color(current->arg);
					}
					break;

				case OUTLINE:
					if (draw_mode == OUTLINE) {
						set_foreground_color(current->arg);
					}
					break;

				case OFFSET:
					w += current->arg;
					last_special_needed = special_index;
					break;

				case VOFFSET:
					cur_y += current->arg;
					break;

				case GOTO:
					if (current->arg >= 0) {
						cur_x = (int) current->arg;
#ifdef BUILD_X11
						//make sure shades are 1 pixel to the right of the text
						if(draw_mode == BG) cur_x++;
#endif
					}
					last_special_needed = special_index;
					break;

				case TAB:
				{
					int start = current->arg;
					int step = current->width;

					if (!step || step < 0) {
						step = 10;
					}
					w = step - (cur_x - text_start_x - start) % step;
					last_special_needed = special_index;
					break;
				}

				case ALIGNR:
				{
					/* TODO: add back in "+ window.border_inner_margin" to the end of
					 * this line? */
					int pos_x = text_start_x + text_width -
						get_string_width_special(s, special_index);

					/* printf("pos_x %i text_start_x %i text_width %i cur_x %i "
						"get_string_width(p) %i gap_x %i "
						"current->arg %i window.border_inner_margin %i "
						"window.border_width %i\n", pos_x, text_start_x, text_width,
						cur_x, get_string_width_special(s), gap_x,
						current->arg, window.border_inner_margin,
						window.border_width); */
					if (pos_x > current->arg && pos_x > cur_x) {
						cur_x = pos_x - current->arg;
					}
					last_special_needed = special_index;
					break;
				}

				case ALIGNC:
				{
					int pos_x = (text_width) / 2 - get_string_width_special(s,
							special_index) / 2 - (cur_x -
								text_start_x);
					/* int pos_x = text_start_x + text_width / 2 -
						get_string_width_special(s) / 2; */

					/* printf("pos_x %i text_start_x %i text_width %i cur_x %i "
						"get_string_width(p) %i gap_x %i "
						"current->arg %i\n", pos_x, text_start_x,
						text_width, cur_x, get_string_width(s), gap_x,
						current->arg); */
					if (pos_x > current->arg) {
						w = pos_x - current->arg;
					}
					last_special_needed = special_index;
					break;
				}
#endif /* BUILD_X11 */
			}

#ifdef BUILD_X11
			cur_x += w;
#endif /* BUILD_X11 */

			if (special_index != last_special_applied) {
				special_index++;
			} else {
				special_index = orig_special_index;
				last_special_applied = -1;
			}
		}
		p++;
	}

#ifdef BUILD_X11
	cur_y += cur_y_add;
#endif /* BUILD_X11 */
	draw_string(s);
#ifdef BUILD_NCURSES
	if (*out_to_ncurses) {
		printw("\n");
	}
#endif /* BUILD_NCURSES */
#ifdef BUILD_X11
	if (*out_to_x)
		cur_y += font_descent();
#endif /* BUILD_X11 */
	return special_index;
}

static int draw_line(char *s, int special_index)
{
#ifdef BUILD_X11
	if (*out_to_x) {
		return draw_each_line_inner(s, special_index, -1);
	}
#endif /* BUILD_X11 */
#ifdef BUILD_NCURSES
	if (*out_to_ncurses) {
		return draw_each_line_inner(s, special_index, -1);
	}
#endif /* BUILD_NCURSES */
	draw_string(s);
	UNUSED(special_index);
	return 0;
}

static void draw_text(void)
{
#ifdef BUILD_HTTP
#define WEBPAGE_START1 "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\" />"
#define WEBPAGE_START2 "<title>Conky</title></head><body style=\"font-family: monospace\"><p>"
#define WEBPAGE_END "</p></body></html>"
	if (*out_to_http) {
		webpage = WEBPAGE_START1;
		if(*http_refresh) {
			webpage.append("<meta http-equiv=\"refresh\" content=\"");
			std::stringstream update_interval_str;
			update_interval_str << *update_interval;
			webpage.append(update_interval_str.str());
			webpage.append("\" />");
		}
		webpage.append(WEBPAGE_START2);
	}
#endif
#ifdef BUILD_X11
	setup_fonts();
#endif /* BUILD_X11 */
#ifdef BUILD_NCURSES
	init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
	attron(COLOR_PAIR(COLOR_WHITE));
#endif /* BUILD_NCURSES */
	for_each_line(text_buffer, draw_line);
#ifdef BUILD_HTTP
	if (*out_to_http) {
		webpage.append(WEBPAGE_END);
	}
#endif
}

static void draw_stuff(void)
{
#ifdef BUILD_IMLIB2
	cimlib_render(text_start_x, text_start_y, window.width, window.height);
#endif /* BUILD_IMLIB2 */
	if (*overwrite_file.size()) {
		overwrite_fpointer = fopen(*overwrite_file.c_str(), "w");
		if(!overwrite_fpointer)
			NORM_ERR("Cannot overwrite '%s'", *overwrite_file.c_str());
	}
	if (*append_file.size()) {
		append_fpointer = fopen(*append_file.c_str(), "a");
		if(!append_fpointer)
			NORM_ERR("Cannot append to '%s'", *append_file.c_str());
	}
	llua_draw_pre_hook();
#ifdef BUILD_X11
	if (*out_to_x) {
		selected_font = 0;
		if (*draw_shades && !*draw_outline) {
			text_start_x++;
			text_start_y++;
			set_foreground_color(*default_shade_color);
			draw_mode = BG;
			draw_text();
			text_start_x--;
			text_start_y--;
		}

		if (*draw_outline) {
			int i, j;
			selected_font = 0;

			for (i = -1; i < 2; i++) {
				for (j = -1; j < 2; j++) {
					if (i == 0 && j == 0) {
						continue;
					}
					text_start_x += i;
					text_start_y += j;
					set_foreground_color(*default_outline_color);
					draw_mode = OUTLINE;
					draw_text();
					text_start_x -= i;
					text_start_y -= j;
				}
			}
		}

		set_foreground_color(*default_color);
	}
#endif /* BUILD_X11 */
	draw_mode = FG;
	draw_text();
	llua_draw_post_hook();
	if(overwrite_fpointer) {
		fclose(overwrite_fpointer);
		overwrite_fpointer = 0;
	}
	if (append_fpointer) {
		fclose(append_fpointer);
		append_fpointer = 0;
	}
}

/* update_text() generates new text and clears old text area */
static void update_text(void)
{
#ifdef BUILD_IMLIB2
	cimlib_cleanup();
#endif /* BUILD_IMLIB2 */
	generate_text();
#ifdef BUILD_X11
	if (*out_to_x)
		clear_text(1);
#endif /* BUILD_X11 */
	need_to_update = 1;
	llua_update_info(&info, active_update_interval());
}

#endif

#ifdef HAVE_SYS_INOTIFY_H
int inotify_fd;
#endif

void old_main_loop(void)
{
	int terminate = 0;
	double t;
#ifdef HAVE_SYS_INOTIFY_H
	int inotify_config_wd = -1;
#define INOTIFY_EVENT_SIZE  (sizeof(struct inotify_event))
#define INOTIFY_BUF_LEN     (20 * (INOTIFY_EVENT_SIZE + 16)) + 1
	char inotify_buff[INOTIFY_BUF_LEN];
#endif /* HAVE_SYS_INOTIFY_H */


	last_update_time = 0.0;
	next_update_time = get_time();
	info.looped = 0;
	while (terminate == 0
			&& (*total_run_times == 0 || info.looped < *total_run_times)) {
		if(*update_interval_on_battery != NOBATTERY) {
			char buf[64];

			get_battery_short_status(buf, 64, "BAT0");
			on_battery = (buf[0] == 'D');
		}
		info.looped++;

#if 0 && BUILD_X11
		if (*out_to_x) {
			XFlush(display);

			/* wait for X event or timeout */

			if (need_to_update) {
				int wx = window.x, wy = window.y;

				need_to_update = 0;
				selected_font = 0;
				update_text_area();

				if (*own_window) {
					int changed = 0;
					int border_total = get_border_total();

					/* update struts */
					if (changed && *own_window_type == TYPE_PANEL) {
						int sidenum = -1;

						fprintf(stderr, _(PACKAGE_NAME": defining struts\n"));
						fflush(stderr);

						switch (*text_alignment) {
							case TOP_LEFT:
							case TOP_RIGHT:
							case TOP_MIDDLE:
								{
									sidenum = 2;
									break;
								}
							case BOTTOM_LEFT:
							case BOTTOM_RIGHT:
							case BOTTOM_MIDDLE:
								{
									sidenum = 3;
									break;
								}
							case MIDDLE_LEFT:
								{
									sidenum = 0;
									break;
								}
							case MIDDLE_RIGHT:
								{
									sidenum = 1;
									break;
								}

							case NONE: case MIDDLE_MIDDLE: /* XXX What about these? */;
						}

						set_struts(sidenum);
					}
				}

				clear_text(1);
			}

			/* handle X events */
			while (XPending(display)) {
				XEvent ev;

				XNextEvent(display, &ev);
				switch (ev.type) {
					case PropertyNotify:
					{
						if ( ev.xproperty.state == PropertyNewValue ) {
							get_x11_desktop_info( ev.xproperty.display, ev.xproperty.atom );
						}
						break;
					}

					case ReparentNotify:
						/* make background transparent */
						if (*own_window) {
							set_transparent_background(window.window);
						}
						break;

					case ConfigureNotify:
						if (*own_window) {
							/* if window size isn't what expected, set fixed size */
							if (ev.xconfigure.width != window.width
									|| ev.xconfigure.height != window.height) {
								if (window.width != 0 && window.height != 0) {
									fixed_size = 1;
								}

								/* clear old stuff before screwing up
								 * size and pos */
								clear_text(1);

								{
									XWindowAttributes attrs;
									if (XGetWindowAttributes(display,
											window.window, &attrs)) {
										window.width = attrs.width;
										window.height = attrs.height;
									}
								}

								int border_total = get_border_total();

								text_width = window.width - 2*border_total;
								text_height = window.height - 2*border_total;
								int mw = *maximum_width;
								if (text_width > mw && mw > 0) {
									text_width = mw;
								}
							}

							/* if position isn't what expected, set fixed pos
							 * total_updates avoids setting fixed_pos when window
							 * is set to weird locations when started */
							/* // this is broken
							if (total_updates >= 2 && !fixed_pos
									&& (window.x != ev.xconfigure.x
									|| window.y != ev.xconfigure.y)
									&& (ev.xconfigure.x != 0
									|| ev.xconfigure.y != 0)) {
								fixed_pos = 1;
							} */
						}
						break;

					case ButtonPress:
						if (*own_window) {
							/* if an ordinary window with decorations */
							if ((*own_window_type == TYPE_NORMAL &&
										not TEST_HINT(*own_window_hints,
													HINT_UNDECORATED)) ||
									*own_window_type == TYPE_DESKTOP) {
								/* allow conky to hold input focus. */
								break;
							} else {
								/* forward the click to the desktop window */
								XUngrabPointer(display, ev.xbutton.time);
								ev.xbutton.window = window.desktop;
								ev.xbutton.x = ev.xbutton.x_root;
								ev.xbutton.y = ev.xbutton.y_root;
								XSendEvent(display, ev.xbutton.window, False,
									ButtonPressMask, &ev);
								XSetInputFocus(display, ev.xbutton.window,
									RevertToParent, ev.xbutton.time);
							}
						}
						break;

					case ButtonRelease:
						if (*own_window) {
							/* if an ordinary window with decorations */
							if ((*own_window_type == TYPE_NORMAL) &&
									not TEST_HINT(*own_window_hints, HINT_UNDECORATED)) {
								/* allow conky to hold input focus. */
								break;
							} else {
								/* forward the release to the desktop window */
								ev.xbutton.window = window.desktop;
								ev.xbutton.x = ev.xbutton.x_root;
								ev.xbutton.y = ev.xbutton.y_root;
								XSendEvent(display, ev.xbutton.window, False,
									ButtonReleaseMask, &ev);
							}
						}
						break;

					default:
						break;
				}
			}

				draw_stuff();
			}
		} else {
#endif /* BUILD_X11 */
			t = (next_update_time - get_time()) * 1000000;
			if(t > 0) usleep((useconds_t)t);
// X11			update_text();
// X11			draw_stuff();
#ifdef BUILD_NCURSES
			if(*out_to_ncurses) {
				refresh();
				clear();
			}
#endif
#if 0 && BUILD_X11
		}
#endif /* BUILD_X11 */

		switch (g_signal_pending) {
			case SIGHUP:
			case SIGUSR1:
				NORM_ERR("received SIGHUP or SIGUSR1. reloading the config file.");
				reload_config();
				break;
			case SIGINT:
			case SIGTERM:
				NORM_ERR("received SIGINT or SIGTERM to terminate. bye!");
				terminate = 1;
				break;
			default:
				/* Reaching here means someone set a signal
				 * (SIGXXXX, signal_handler), but didn't write any code
				 * to deal with it.
				 * If you don't want to handle a signal, don't set a handler on
				 * it in the first place. */
				if (g_signal_pending) {
					NORM_ERR("ignoring signal (%d)", g_signal_pending);
				}
				break;
		}
#ifdef HAVE_SYS_INOTIFY_H
		if (!*disable_auto_reload && inotify_fd != -1
						&& inotify_config_wd == -1 && !current_config.empty()) {
			inotify_config_wd = inotify_add_watch(inotify_fd,
					current_config.c_str(),
					IN_MODIFY);
		}
		if (!*disable_auto_reload && inotify_fd != -1
						&& inotify_config_wd != -1 && !current_config.empty()) {
			int len = 0, idx = 0;
			fd_set descriptors;
			struct timeval time_to_wait;

			FD_ZERO(&descriptors);
			FD_SET(inotify_fd, &descriptors);

			time_to_wait.tv_sec = time_to_wait.tv_usec = 0;

			select(inotify_fd + 1, &descriptors, NULL, NULL, &time_to_wait);
			if (FD_ISSET(inotify_fd, &descriptors)) {
				/* process inotify events */
				len = read(inotify_fd, inotify_buff, INOTIFY_BUF_LEN - 1);
				inotify_buff[len] = 0;
				while (len > 0 && idx < len) {
					struct inotify_event *ev = (struct inotify_event *) &inotify_buff[idx];
					if (ev->wd == inotify_config_wd && (ev->mask & IN_MODIFY || ev->mask & IN_IGNORED)) {
						/* current_config should be reloaded */
						NORM_ERR("'%s' modified, reloading...", current_config.c_str());
						reload_config();
						if (ev->mask & IN_IGNORED) {
							/* for some reason we get IN_IGNORED here
							 * sometimes, so we need to re-add the watch */
							inotify_config_wd = inotify_add_watch(inotify_fd,
									current_config.c_str(),
									IN_MODIFY);
						}
						break;
					}
					else {
						llua_inotify_query(ev->wd, ev->mask);
					}
					idx += INOTIFY_EVENT_SIZE + ev->len;
				}
			}
		} else if (*disable_auto_reload && inotify_fd != -1) {
			inotify_rm_watch(inotify_fd, inotify_config_wd);
			close(inotify_fd);
			inotify_fd = inotify_config_wd = 0;
		}
#endif /* HAVE_SYS_INOTIFY_H */

		llua_update_info(&info, active_update_interval());
		g_signal_pending = 0;
	}
	clean_up(NULL, NULL);

#ifdef HAVE_SYS_INOTIFY_H
	if (inotify_fd != -1) {
		inotify_rm_watch(inotify_fd, inotify_config_wd);
		close(inotify_fd);
		inotify_fd = inotify_config_wd = 0;
	}
#endif /* HAVE_SYS_INOTIFY_H */
}

void initialisation(int argc, char** argv);

	/* reload the config file */
static void reload_config(void)
{
	struct stat sb;
	if (stat(current_config.c_str(), &sb) || (!S_ISREG(sb.st_mode) && !S_ISLNK(sb.st_mode))) {
		NORM_ERR(_("Config file '%s' is gone, continuing with config from memory.\nIf you recreate this file sent me a SIGUSR1 to tell me about it. ( kill -s USR1 %d )"), current_config.c_str(), getpid());
		return;
	}
	clean_up(NULL, NULL);
	state.reset(new lua::state);
	conky::export_symbols(*state);
	sleep(1); /* slight pause */
	initialisation(argc_copy, argv_copy);
}

void free_specials(special_t *&current) {
	if (current) {
		free_specials(current->next);
		if(current->type == GRAPH)
			free(current->graph);
		delete current;
		current = NULL;
	}
}

void clean_up_without_threads(void *memtofree1, void* memtofree2)
{
	free_and_zero(memtofree1);
	free_and_zero(memtofree2);

	free_and_zero(info.cpu_usage);

	if (info.first_process) {
		free_all_processes();
		info.first_process = NULL;
	}

	free_text_objects(&global_root_object);
	global_text.reset();

#ifdef BUILD_PORT_MONITORS
	tcp_portmon_clear();
#endif
	llua_shutdown_hook();
#if defined BUILD_WEATHER_XOAP || defined BUILD_RSS
	xmlCleanupParser();
#endif

	free_specials(specials);

	clear_net_stats();
	clear_diskio_stats();
	free_and_zero(global_cpu);

	conky::cleanup_config_settings(*state);
	state.reset();
}

void clean_up(void *memtofree1, void* memtofree2)
{
	/* free_update_callbacks(); XXX: some new equivalent of this? */
	clean_up_without_threads(memtofree1, memtofree2);
}

static void set_default_configurations(void)
{
	update_uname();
	info.memmax = 0;
	top_cpu = 0;
	top_mem = 0;
	top_time = 0;
#ifdef BUILD_IOSTATS
	top_io = 0;
#endif
	top_running = 0;
#ifdef BUILD_XMMS2
	info.xmms2.artist = NULL;
	info.xmms2.album = NULL;
	info.xmms2.title = NULL;
	info.xmms2.genre = NULL;
	info.xmms2.comment = NULL;
	info.xmms2.url = NULL;
	info.xmms2.status = NULL;
	info.xmms2.playlist = NULL;
#endif /* BUILD_XMMS2 */
	state->pushboolean(true);
#ifdef BUILD_X11
	conky::out_to_x.lua_set(*state);
#else
	conky::out_to_stdout.lua_set(*state);
#endif

	info.users.number = 1;
}

void load_config_file()
{
	DBGP(_("reading contents from config file '%s'"), current_config.c_str());

	lua::state &l = *state;
	lua::stack_sentry s(l);
	l.checkstack(2);

	try {
#ifdef BUILD_BUILTIN_CONFIG
		if(current_config == builtin_config_magic)
			l.loadstring(defconfig);
		else
#endif
			l.loadfile(current_config.c_str());
	}
	catch(lua::syntax_error &e) {
#define SYNTAX_ERR_READ_CONF "Syntax error (%s) while reading config file. "
#ifdef BUILD_OLD_CONFIG
		NORM_ERR(_(SYNTAX_ERR_READ_CONF), e.what());
		NORM_ERR(_("Assuming it's in old syntax and attempting conversion."));
		// the strchr thingy skips the first line (#! /usr/bin/lua)
		l.loadstring(strchr(convertconf, '\n'));
		l.pushstring(current_config.c_str());
		l.call(1, 1);
#else
		throw conky::error(strprintf(_(SYNTAX_ERR_READ_CONF), e.what()));
#endif
	}
	l.call(0, 0);

	l.getglobal("conky");
	l.getfield(-1, "text");
	l.replace(-2);
	global_text = conky::layout_item::create(l);
	if(not global_text)
		throw conky::error(_("missing or corrupt text block in configuration"));
	
#if 0
#if defined(BUILD_NCURSES)
#if defined(BUILD_X11)
	if (*out_to_x && *out_to_ncurses) {
		NORM_ERR("out_to_x and out_to_ncurses are incompatible, turning out_to_ncurses off");
		state->pushboolean(false);
		out_to_ncurses.lua_set(*state);
	}
#endif /* BUILD_X11 */
	if ((*out_to_stdout || *out_to_stderr)
			&& *out_to_ncurses) {
		NORM_ERR("out_to_ncurses conflicts with out_to_console and out_to_stderr, disabling the later ones");
		// XXX: this will need some rethinking
		state->pushboolean(false);
		out_to_stdout.lua_set(*state);
		state->pushboolean(false);
		out_to_stderr.lua_set(*state);
	}
#endif /* BUILD_NCURSES */
#endif
}

static void print_help(const char *prog_name) {
	printf("Usage: %s [OPTION]...\n"
			PACKAGE_NAME " is a system monitor that renders text on desktop or to own transparent\n"
			"window. Command line options will override configurations defined in config\n"
			"file.\n"
			"   -v, --version             version\n"
			"   -q, --quiet               quiet mode\n"
			"   -D, --debug               increase debugging output, ie. -DD for more debugging\n"
			"   -c, --config=FILE         config file to load\n"
#ifdef BUILD_BUILTIN_CONFIG
			"   -C, --print-config        print the builtin default config to stdout\n"
			"                             e.g. 'conky -C > ~/.conkyrc' will create a new default config\n"
#endif
			"   -d, --daemonize           daemonize, fork to background\n"
			"   -h, --help                help\n"
#ifdef BUILD_X11
			"   -a, --alignment=ALIGNMENT text alignment on screen, {top,bottom,middle}_{left,right,middle}\n"
			"   -f, --font=FONT           font to use\n"
			"   -X, --display=DISPLAY     X11 display to use\n"
			"   -o, --own-window          create own window to draw\n"
			"   -b, --double-buffer       double buffer (prevents flickering)\n"
			"   -w, --window-id=WIN_ID    window id to draw\n"
			"   -x X                      x position\n"
			"   -y Y                      y position\n"
#endif /* BUILD_X11 */
			"   -t, --text=TEXT           text to render, remember single quotes, like -t '$uptime'\n"
			"   -u, --interval=SECS       update interval\n"
			"   -i COUNT                  number of times to update " PACKAGE_NAME " (and quit)\n"
			"   -p, --pause=SECS          pause for SECS seconds at startup before doing anything\n",
			prog_name
	);
}

inline void reset_optind() {
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__OpenBSD__) \
	|| defined(__NetBSD__) || defined(__DragonFly__)
	optind = optreset = 1;
#else
	optind = 0;
#endif
}

/* : means that character before that takes an argument */
static const char *getopt_string = "vVqdDSs:t:u:i:hc:p:"
#ifdef BUILD_X11
	"x:y:w:a:f:X:"
	"o"
	"b"
#endif /* BUILD_X11 */
#ifdef BUILD_BUILTIN_CONFIG
	"C"
#endif
	;

static const struct option longopts[] = {
	{ "help", 0, NULL, 'h' },
	{ "version", 0, NULL, 'V' },
	{ "quiet", 0, NULL, 'q' },
	{ "debug", 0, NULL, 'D' },
	{ "config", 1, NULL, 'c' },
#ifdef BUILD_BUILTIN_CONFIG
	{ "print-config", 0, NULL, 'C' },
#endif
	{ "daemonize", 0, NULL, 'd' },
#ifdef BUILD_X11
	{ "alignment", 1, NULL, 'a' },
	{ "font", 1, NULL, 'f' },
	{ "display", 1, NULL, 'X' },
	{ "own-window", 0, NULL, 'o' },
	{ "double-buffer", 0, NULL, 'b' },
	{ "window-id", 1, NULL, 'w' },
#endif /* BUILD_X11 */
	{ "text", 1, NULL, 't' },
	{ "interval", 1, NULL, 'u' },
	{ "pause", 1, NULL, 'p' },
	{ 0, 0, 0, 0 }
};

void set_current_config() {
	/* load current_config, CONFIG_FILE or SYSTEM_CONFIG_FILE */
	struct stat s;

	if (current_config.empty()) {
		/* Try to use personal config file first */
		std::string buf = to_real_path(CONFIG_FILE);
		if (stat(buf.c_str(), &s) == 0)
			current_config = buf;
	}

	/* Try to use system config file if personal config does not exist */
	if (current_config.empty() && (stat(SYSTEM_CONFIG_FILE, &s)==0))
		current_config = SYSTEM_CONFIG_FILE;

	/* No readable config found */
	if (current_config.empty()) {
#define NOCFGFILEFOUND "no personal or system-wide config file found"
#ifdef BUILD_BUILTIN_CONFIG
		current_config = builtin_config_magic;
		NORM_ERR(NOCFGFILEFOUND ", using builtin default");
#else
		throw conky::error(NOCFGFILEFOUND);
#endif
	}

	// "-" stands for "read from stdin"
	if(current_config == "-")
		current_config = "/dev/stdin";
}

void initialisation(int argc, char **argv) {
	struct sigaction act, oact;

	set_default_configurations();

	set_current_config();
	load_config_file();

	/* handle other command line arguments */

	reset_optind();

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
	if ((kd = kvm_open("/dev/null", "/dev/null", "/dev/null", O_RDONLY,
			"kvm_open")) == NULL) {
		CRIT_ERR(NULL, NULL, "cannot read kvm");
	}
#endif

	while (1) {
		int c = getopt_long(argc, argv, getopt_string, longopts, NULL);
		int startup_pause;

		if (c == -1) {
			break;
		}

		switch (c) {
			case 'd':
				state->pushboolean(true);
				fork_to_background.lua_set(*state);
				break;
#if 0 && BUILD_X11
			case 'f':
				state->pushstring(optarg);
				font.lua_set(*state);
				break;
			case 'a':
				state->pushstring(optarg);
				text_alignment.lua_set(*state);
				break;
			case 'X':
				state->pushstring(optarg);
				display_name.lua_set(*state);
				break;
			case 'o':
				state->pushboolean(true);
				own_window.lua_set(*state);
				break;
#ifdef BUILD_XDBE
			case 'b':
				state->pushboolean(true);
				use_xdbe.lua_set(*state);
				break;
#else
			case 'b':
				state->pushboolean(true);
				use_xpmdb.lua_set(*state);
				break;
#endif
#endif /* BUILD_X11 */
#if 0 // XXX
			case 't':
				free_and_zero(global_text);
				global_text = strndup(optarg, *max_user_text);
				convert_escapes(global_text);
				break;
#endif
			case 'u':
				state->pushstring(optarg);
				update_interval.lua_set(*state);
				break;

			case 'i':
				state->pushstring(optarg);
				total_run_times.lua_set(*state);
				break;
#if 0
			case 'x':
				state->pushstring(optarg);
				gap_x.lua_set(*state);
				break;

			case 'y':
				state->pushstring(optarg);
				gap_y.lua_set(*state);
				break;
#endif /* BUILD_X11 */
			case 'p':
				if (first_pass) {
					startup_pause = atoi(optarg);
					sleep(startup_pause);
				}
				break;

			case '?':
				throw unknown_arg_throw();
		}
	}

	conky::set_config_settings(*state);

	/* fork */
	if (*fork_to_background && first_pass) {
		int pid = fork();

		switch (pid) {
			case -1:
				NORM_ERR(PACKAGE_NAME": couldn't fork() to background: %s",
					strerror(errno));
				break;

			case 0:
				/* child process */
				usleep(25000);
				fprintf(stderr, "\n");
				fflush(stderr);
				break;

			default:
				/* parent process */
				fprintf(stderr, PACKAGE_NAME": forked to background, pid is %d\n",
					pid);
				fflush(stderr);
				throw fork_throw();
		}
	}

	llua_setup_info(&info, active_update_interval());
#ifdef BUILD_WEATHER_XOAP
	xmlInitParser();
#endif /* BUILD_WEATHER_XOAP */

	/* Set signal handlers */
	act.sa_handler = signal_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
#ifdef SA_RESTART
	act.sa_flags |= SA_RESTART;
#endif

	if (		sigaction(SIGINT,  &act, &oact) < 0
			||	sigaction(SIGALRM, &act, &oact) < 0
			||	sigaction(SIGUSR1, &act, &oact) < 0
			||	sigaction(SIGHUP,  &act, &oact) < 0
			||	sigaction(SIGTERM, &act, &oact) < 0) {
		NORM_ERR("error setting signal handler: %s", strerror(errno));
	}

	llua_startup_hook();
}

static void main_loop()
{
	// block some signals
	// we poll them later manually
	sigset_t sigmask;
	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGINT);
	pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

	auto last_update = std::chrono::high_resolution_clock::now();

	while(true) {
		struct timespec timeout;

		// handle signals
		timeout.tv_sec = 0;
		timeout.tv_nsec = 0;

		auto now = std::chrono::high_resolution_clock::now();
		std::chrono::milliseconds aui((int)(1000*active_update_interval()));
		if(last_update <= now && (now-last_update) < aui) {
			auto sleep_for = aui - (now-last_update);

			auto sec = std::chrono::duration_cast<std::chrono::seconds>(sleep_for);
			timeout.tv_sec = sec.count();
			sleep_for -= sec;

			timeout.tv_nsec
				= std::chrono::duration_cast<std::chrono::nanoseconds>(sleep_for).count();
		}
		switch(sigtimedwait(&sigmask, NULL, &timeout)) {
			case SIGINT:
				return;

			case -1:
				if(errno == EAGAIN) // timeout expired
					break;
				if(errno!=EINTR)
					throw errno_error("sigtimedwait");
				// else, retry
				continue;
		}

		// time to collect new data
		// XXX conky::run_all_callbacks();
		conky::output_methods.run_all_threads();
		last_update = std::chrono::high_resolution_clock::now();
	}
}

int main(int argc, char **argv)
{
#ifdef BUILD_I18N
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE_NAME, LOCALE_DIR);
	textdomain(PACKAGE_NAME);
#endif
	argc_copy = argc;
	argv_copy = argv;
	g_signal_pending = 0;
	clear_net_stats();

#ifdef BUILD_CURL
	struct curl_global_initializer {
		curl_global_initializer()
		{
			if(curl_global_init(CURL_GLOBAL_ALL))
				NORM_ERR("curl_global_init() failed, you may not be able to use curl variables");
		}
		~curl_global_initializer()
		{ curl_global_cleanup(); }
	};
	curl_global_initializer curl_global;
#endif

	/* handle command line parameters that don't change configs */
	while (1) {
		int c = getopt_long(argc, argv, getopt_string, longopts, NULL);

		if (c == -1) {
			break;
		}

		switch (c) {
			case 'D':
				global_debug_level++;
				break;
			case 'v':
			case 'V':
				print_version();
				return EXIT_SUCCESS;
			case 'c':
				current_config = optarg;
				break;
			case 'q':
				if (!freopen("/dev/null", "w", stderr))
					CRIT_ERR(0, 0, "could not open /dev/null as stderr!");
				break;
			case 'h':
				print_help(argv[0]);
				return 0;
#ifdef BUILD_BUILTIN_CONFIG
			case 'C':
				std::cout << defconfig;
				return 0;
#endif
#if 0 // XXX has this ever worked?
			case 'w':
				window.window = strtol(optarg, 0, 0);
				break;
#endif /* BUILD_X11 */

			case '?':
				return EXIT_FAILURE;
		}
	}

	try {
		set_current_config();

		state.reset(new lua::state);

		conky::export_symbols(*state);

#ifdef BUILD_WEATHER_XOAP
		/* Load xoap keys, if existing */
		load_xoap_keys();
#endif /* BUILD_WEATHER_XOAP */

#ifdef HAVE_SYS_INOTIFY_H
		// the file descriptor will be automatically closed on exit
		inotify_fd = inotify_init();
		if(inotify_fd != -1) {
			fcntl(inotify_fd, F_SETFL, fcntl(inotify_fd, F_GETFL) | O_NONBLOCK);

			fcntl(inotify_fd, F_SETFD, fcntl(inotify_fd, F_GETFD) | FD_CLOEXEC);
		}
#endif /* HAVE_SYS_INOTIFY_H */

		initialisation(argc, argv);

		first_pass = 0; /* don't ever call fork() again */

		main_loop();
	}
	catch(fork_throw &e) { return EXIT_SUCCESS; }
	catch(unknown_arg_throw &e) { return EXIT_FAILURE; }
	catch(obj_create_error &e) {
		std::cerr << e.what() << std::endl;
		clean_up(NULL, NULL);
		return EXIT_FAILURE;
	}
	catch(std::exception &e) {
		std::cerr << PACKAGE_NAME": " << e.what() << std::endl;
		return EXIT_FAILURE;
	}

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
	kvm_close(kd);
#endif

#ifdef LEAKFREE_NCURSES
	_nc_free_and_exit(0);	//hide false memleaks
#endif
	return 0;

}

static void signal_handler(int sig)
{
	/* signal handler is light as a feather, as it should be.
	 * we will poll g_signal_pending with each loop of conky
	 * and do any signal processing there, NOT here */

	g_signal_pending = sig;
}
