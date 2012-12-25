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
#include "unicode.hh"
#include "colours.h"

#define ATOM(a) XInternAtom(display, #a, False)

enum window_type {
	TYPE_NORMAL = 0,
	TYPE_DOCK,
	TYPE_PANEL,
	TYPE_DESKTOP,
	TYPE_OVERRIDE
};

enum window_hints {
	HINT_UNDECORATED = 1,
	HINT_BELOW = HINT_UNDECORATED << 1,
	HINT_ABOVE = HINT_BELOW << 1,
	HINT_STICKY = HINT_ABOVE << 1,
	HINT_SKIP_TASKBAR = HINT_STICKY << 1,
	HINT_SKIP_PAGER = HINT_SKIP_TASKBAR << 1
};

struct conky_window {
	Window root, window /*XXX*/, desktop;
	GC gc;

#ifdef BUILD_XDBE
	XdbeBackBuffer back_buffer;
#else
	Pixmap back_buffer;
#endif
#ifdef BUILD_XFT
	XftDraw *xftdraw;
#endif

	int width, height; // XXX
};

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

namespace conky {

	class x11_output: public output_method {
		class colour {
		protected:
			const unsigned long pixel;

		public:
			explicit colour(unsigned long pixel_) : pixel(pixel_) {}
			virtual ~colour() { }

			unsigned long get_pixel() const { return pixel; }
		};

		class colour_factory {
		protected:
			Display &display;
			const Colormap colourmap;

		public:
			colour_factory(Display &display_, Colormap colourmap_)
				: display(display_), colourmap(colourmap_)
			{ }

			virtual ~colour_factory() { }

			std::shared_ptr<colour> get_colour(const char *name);

			// One should implement at least one of the following two functions, because the
			// default implementations just call each other
			virtual std::shared_ptr<colour>
			get_colour(uint16_t red, uint16_t green, uint16_t blue);

			virtual std::shared_ptr<colour> get_colour(XColor &colour)
			{
				auto t = get_colour(colour.red, colour.green, colour.blue);
				colour.pixel = t->get_pixel();
				return t;
			}

			static std::unique_ptr<colour_factory>
			create(Display &display, Visual &visual, Colormap colourmap);
		};

		class true_colour_factory: public colour_factory {
			uint8_t red_shift;
			uint8_t green_shift;
			uint8_t blue_shift;
			uint8_t rgb_bits;

		public:
			true_colour_factory(Display &display_, Visual &visual, Colormap colourmap_)
				: colour_factory(display_, colourmap_), red_shift(ffs(visual.red_mask)-1),
				  green_shift(ffs(visual.green_mask)-1), blue_shift(ffs(visual.blue_mask)-1),
				  rgb_bits(visual.bits_per_rgb)
			{ }

			virtual std::shared_ptr<colour>
			get_colour(uint16_t red, uint16_t green, uint16_t blue);
		};

		class alloc_colour_factory: public colour_factory {
			class alloc_colour: public colour {
				const alloc_colour_factory &factory;

			public:
				alloc_colour(unsigned long pixel, const alloc_colour_factory &factory_)
					: colour(pixel), factory(factory_)
				{ }

				virtual ~alloc_colour()
				{
					// const_cast is here because of a stupid interface
					XFreeColors(&factory.display, factory.colourmap,
							const_cast<unsigned long *>(&pixel), 1, 0);
				}
			};

			std::shared_ptr<colour> white;
		public:
			alloc_colour_factory(Display &display_, Colormap colourmap_);
			virtual std::shared_ptr<colour> get_colour(XColor &colour);
		};

		// window classes
		class window_handler;
		typedef window_handler root_window_handler;
		class own_window_handler;

		// (double) buffering classes
		class buffer;
		typedef buffer single_buffer;
		class double_buffer;
		class xdbe_buffer;
		class pixmap_buffer;

		unicode_converter conv;

		Display *display;
		point display_size;
		int screen;
		Window root;
		Window desktop;
		std::unique_ptr<window_handler> window;
		Visual *visual;
		int depth;
		Colormap colourmap;
		point window_size;
		point position;
		std::unique_ptr<buffer> drawable;

		XFontSet fontset;
		const XRectangle *font_extents;

		std::unique_ptr<colour_factory> colours;
		std::shared_ptr<colour> fg_colour;

		Window find_subwindow(Window win);
		void find_root_and_desktop_window();
		void create_window(bool override);
		void create_fontset();

		void process_events();
	protected:
		virtual void work();

	public:
		enum class buffer_type { SINGLE, PIXMAP, XDBE };

		x11_output(uint32_t period, const std::string &display_);
		~x11_output();

		virtual point get_max_extents() const
		{ return { font_extents->width - font_extents->x, font_extents->height - font_extents->y}; }

		virtual point get_text_size(const std::string &text) const;
		virtual point get_text_size(const std::u32string &text) const;
		virtual void draw_text(const std::string &text, const point &p, const point &size);
		virtual void draw_text(const std::u32string &text, const point &p, const point &size);

		bool set_visual(bool argb);
		void use_root_window();
		void use_own_window();
		buffer_type setup_buffer(buffer_type type);
	};

	namespace priv {
		class out_to_x_setting: public simple_config_setting<bool> {
			typedef simple_config_setting<bool> Base;

			thread_handle<x11_output> om;
		protected:
			virtual void cleanup()
			{ om.reset(); }

		public:
			out_to_x_setting()
				: Base("out_to_x", true, false)
			{}

			virtual const bool set(const bool &r, bool init);

			const thread_handle<x11_output>& get_om()
			{ return om; }
		};

		class use_argb_visual_setting: public simple_config_setting<bool> {
			typedef simple_config_setting<bool> Base;

		public:
			const bool set(const bool &r, bool init);
			use_argb_visual_setting()
				: Base("own_window_argb_visual", false, false)
			{}
		};

		class own_window_setting: public simple_config_setting<bool> {
			typedef simple_config_setting<bool> Base;

		public:
			const bool set(const bool &r, bool init);
			own_window_setting()
				: Base("own_window", false, false)
			{}
		};

		struct window_hints_traits {
			typedef lua_traits<window_hints> Traits;

			static uint16_t from_lua(lua::state &l, int index, const std::string &description);
			static void to_lua(lua::state &l, uint16_t t, const std::string &description);
		};
	} /* namespace conky::priv */
} /* namespace conky */

namespace priv {
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

extern conky::simple_config_setting<std::string> display_name;
extern conky::priv::out_to_x_setting             out_to_x;
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

extern conky::simple_config_setting<bool>        set_transparent;
extern conky::simple_config_setting<std::string> own_window_class;
extern conky::simple_config_setting<std::string> own_window_title;
extern conky::simple_config_setting<window_type> own_window_type;

extern conky::simple_config_setting<uint16_t, conky::priv::window_hints_traits> own_window_hints;

extern conky::priv::use_argb_visual_setting      use_argb_visual;
#ifdef BUILD_ARGB

/* range of 0-255 for alpha */
extern conky::range_config_setting<int>          own_window_argb_value;
#endif
extern conky::priv::own_window_setting			 own_window;

#endif /*X11_H_*/
#endif /* BUILD_X11 */
