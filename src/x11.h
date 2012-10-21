/* -*- mode: c++; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: t -*-
 * vim: ts=4 sw=4 noet ai cindent syntax=cpp
 *
 * Conky, a system monitor, based on torsmo
 *
 * Please see COPYING for details
 *
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

#ifdef BUILD_X11
#ifndef X11_H_
#define X11_H_

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#ifdef BUILD_XFT
#include <X11/Xft/Xft.h>
#endif

#ifdef BUILD_XDBE
#include <X11/extensions/Xdbe.h>
#endif

#include "output-method.hh"
#include "setting.hh"
#include "colours.h"

#define ATOM(a) XInternAtom(display, #a, False)

#ifdef OWN_WINDOW
enum window_type {
	TYPE_NORMAL = 0,
	TYPE_DOCK,
	TYPE_PANEL,
	TYPE_DESKTOP,
	TYPE_OVERRIDE
};

enum window_hints {
	HINT_UNDECORATED = 0,
	HINT_BELOW,
	HINT_ABOVE,
	HINT_STICKY,
	HINT_SKIP_TASKBAR,
	HINT_SKIP_PAGER
};

#define SET_HINT(mask, hint)	(mask |= (1 << (hint)))
#define TEST_HINT(mask, hint)	(mask & (1 << (hint)))
#endif

struct conky_window {
	Window root, window, desktop;
	Drawable drawable;
	Visual *visual;
	Colormap colourmap;
	GC gc;

#ifdef BUILD_XDBE
	XdbeBackBuffer back_buffer;
#else
	Pixmap back_buffer;
#endif
#ifdef BUILD_XFT
	XftDraw *xftdraw;
#endif

	int width;
	int height;
#ifdef OWN_WINDOW
	int x;
	int y;
#endif
};

#if defined(BUILD_ARGB) && defined(OWN_WINDOW)
/* true if use_argb_visual=true and argb visual was found*/
extern bool have_argb_visual;
#endif

extern Display *display;
extern int display_width;
extern int display_height;
extern int screen;

extern int workarea[4];

extern struct conky_window window;
extern char window_created;

void destroy_window(void);
void create_gc(void);
void set_transparent_background(Window win);
void get_x11_desktop_info(Display *display, Atom atom);
void set_struts(int);

void print_monitor(struct text_object *, char *, int);
void print_monitor_number(struct text_object *, char *, int);
void print_desktop(struct text_object *, char *, int);
void print_desktop_number(struct text_object *, char *, int);
void print_desktop_name(struct text_object *, char *, int);

#ifdef BUILD_XDBE
void xdbe_swap_buffers(void);
#else
void xpmdb_swap_buffers(void);
#endif /* BUILD_XDBE */

/* alignments */
enum alignment {
	TOP_LEFT,
	TOP_RIGHT,
	TOP_MIDDLE,
	BOTTOM_LEFT,
	BOTTOM_RIGHT,
	BOTTOM_MIDDLE,
	MIDDLE_LEFT,
	MIDDLE_MIDDLE,
	MIDDLE_RIGHT,
	NONE
};

extern conky::simple_config_setting<alignment>   text_alignment;

namespace priv {
	class out_to_x_setting: public conky::simple_config_setting<bool> {
		typedef conky::simple_config_setting<bool> Base;
	
	protected:
		virtual void cleanup();

	public:
		virtual const bool set(const bool &r, bool init);
		out_to_x_setting()
			: Base("out_to_x", true, false)
		{}
	};
	
	class own_window_setting: public conky::simple_config_setting<bool> {
		typedef conky::simple_config_setting<bool> Base;
	
	public:
		const bool set(const bool &r, bool init);
		own_window_setting()
			: Base("own_window", false, false)
		{}
	};

	class use_xdbe_setting: public conky::simple_config_setting<bool> {
		typedef conky::simple_config_setting<bool> Base;
	
		bool set_up();

	public:
		virtual const bool set(const bool &r, bool init);
		use_xdbe_setting()
			: Base("double_buffer", false, false)
		{}
	};

	class use_xpmdb_setting: public conky::simple_config_setting<bool> {
		typedef conky::simple_config_setting<bool> Base;
	
		bool set_up();

	public:
		virtual const bool set(const bool &r, bool init);
		use_xpmdb_setting()
			: Base("double_buffer", false, false)
		{}
	};

	
	struct colour_traits {
		static unsigned long
		from_lua(lua::state &l, int index, const std::string &);

		static void to_lua(lua::state &l, unsigned long, const std::string &)
		{ l.pushstring("Operation not supported (yet)"); }
	};

	class colour_setting: public conky::simple_config_setting<unsigned long, colour_traits> {
		typedef conky::simple_config_setting<unsigned long, colour_traits> Base;
	
	public:
		colour_setting(const std::string &name_, unsigned long default_value_ = 0)
			: Base(name_, default_value_, true)
		{}
		
	};
}

extern priv::out_to_x_setting                    out_to_x;
extern conky::simple_config_setting<std::string> display_name;
extern priv::colour_setting						 color[10];
extern priv::colour_setting						 default_color;
extern priv::colour_setting						 default_shade_color;
extern priv::colour_setting						 default_outline_color;

extern conky::range_config_setting<int>          border_inner_margin;
extern conky::range_config_setting<int>          border_outer_margin;
extern conky::range_config_setting<int>          border_width;

#ifdef BUILD_XFT
extern conky::simple_config_setting<bool>        use_xft;
#endif

#ifdef OWN_WINDOW
extern conky::simple_config_setting<bool>        set_transparent;
extern conky::simple_config_setting<std::string> own_window_class;
extern conky::simple_config_setting<std::string> own_window_title;
extern conky::simple_config_setting<window_type> own_window_type;

struct window_hints_traits {
	static uint16_t from_lua(lua::state &l, int index, const std::string &description);
};
extern conky::simple_config_setting<uint16_t, window_hints_traits> own_window_hints;

#ifdef BUILD_ARGB
extern conky::simple_config_setting<bool>        use_argb_visual;

/* range of 0-255 for alpha */
extern conky::range_config_setting<int>          own_window_argb_value;
#endif
#endif /*OWN_WINDOW*/
extern priv::own_window_setting					 own_window;

#ifdef BUILD_XDBE
extern priv::use_xdbe_setting					 use_xdbe;
#else
extern priv::use_xpmdb_setting					 use_xpmdb;
#endif

namespace conky {

	class x11_output: public output_method {
	public:
		virtual point get_text_size(const std::string &text) const;
		virtual point get_text_size(const std::u32string &text) const;
		virtual void draw_text(const std::string &text, const point &p, const point &size);
		virtual void draw_text(const std::u32string &text, const point &p, const point &size);
	};

}

#endif /*X11_H_*/
#endif /* BUILD_X11 */
