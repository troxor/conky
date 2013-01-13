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

#include <cmath>

#include "conky.h"
#include "logging.h"
#include "common.h"

#include "x11.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <X11/Xutil.h>
#ifdef BUILD_IMLIB2
#include "imlib2.h"
#endif /* BUILD_IMLIB2 */
#ifdef BUILD_XFT
#include <X11/Xft/Xft.h>
#endif
#ifdef BUILD_XDBE
#include <X11/extensions/Xdbe.h>
#endif


// XXX: TEMPORARY stub variables
Display *display;
int display_width;
int display_height;
/* Window stuff */
struct conky_window window;


/********************* <SETTINGS> ************************/
namespace conky {
	typedef x11_output::buffer_type buffer_type;
	template<>
	conky::lua_traits<buffer_type>::Map conky::lua_traits<buffer_type>::map = {
		{ "xdbe",   buffer_type::XDBE },
		{ "pixmap", buffer_type::PIXMAP },
		{ "single", buffer_type::SINGLE },
		{ "yes",    buffer_type::XDBE },
		{ "no",     buffer_type::SINGLE }
	};

	namespace {
		template<typename T>
		class fancy_x11_setting: public simple_config_setting<T> {
			typedef simple_config_setting<T> Base;
			typedef T (x11_output::*Fun)(T);

			Fun fun;

		public:
			fancy_x11_setting(const char *name_, const T &default_value_, Fun fun_)
				: Base(name_, default_value_, false), fun(fun_)
			{ }

			virtual const T set(const T &r, bool init)
			{
				assert(init);

				if(*out_to_x) {
					Base::value = (*out_to_x.get_om().*fun)(r);
				} else
					Base::value = Base::default_value;

				return Base::value;
			}
		};

		class double_buffer_setting: public fancy_x11_setting<buffer_type> {
			typedef fancy_x11_setting<buffer_type> Base;

		protected:
			virtual std::pair<buffer_type, bool> do_convert(lua::state &l, int index)
			{
				if(l.type(index) == lua::TBOOLEAN)
					return { l.toboolean(index) ? buffer_type::XDBE : buffer_type::SINGLE, true };
				else
					return Base::do_convert(l, index);
			}

		public:
			double_buffer_setting()
				: Base("double_buffer", buffer_type::SINGLE, &x11_output::setup_buffer)
			{}
		};

		struct background_colour_traits {
			static inline std::shared_ptr<x11_output::colour>
			from_lua(lua::state &l, int index, const std::string &description)
			{
				type_check(l, index, lua::TSTRING, lua::TNUMBER, description);

				return out_to_x.get_om()->get_colour(l.tocstring(index),
											lround(*own_window_argb_value * 255));
			}

			static inline void
			to_lua(lua::state &l, const std::shared_ptr<x11_output::colour> &colour,
					const std::string &)
			{
				const XColor &c = out_to_x.get_om()->get_rgb(colour);
				l.pushstring(strprintf("rgb:%04x/%04x/%04x", c.red, c.green, c.blue));
			}
		};

		class background_colour_setting: public simple_config_setting<
											std::shared_ptr<x11_output::colour>,
											background_colour_traits> {

			typedef simple_config_setting<std::shared_ptr<x11_output::colour>,
											background_colour_traits> Base;

			std::string default_name;
		public:
			background_colour_setting(const std::string &name_, const std::string &default_name_)
				: Base(name_, std::shared_ptr<x11_output::colour>(), true),
				  default_name(default_name_)
			{}

			virtual const std::shared_ptr<x11_output::colour> set_default(bool)
			{
				return value = out_to_x.get_om()->get_colour(default_name.c_str(),
													lround(*own_window_argb_value * 255));
			}
		};
	} /* anonymous namespace */

	namespace priv {
		const bool out_to_x_setting::set(const bool &r, bool init)
		{
			assert(init);

			if(r)
				om = output_methods.register_thread<x11_output>(1, *display_name);

			return value = r;
		}

		uint16_t
		window_hints_traits::from_lua(lua::state &l, int index, const std::string &description)
		{
			lua::stack_sentry s(l);
			l.checkstack(1);

			std::string hints = l.tostring(index);
			// add a sentinel to simplify the following loop
			hints += ',';
			size_t pos = 0;
			size_t newpos;
			uint16_t ret = 0;
			while((newpos = hints.find_first_of(", ", pos)) != std::string::npos) {
				if(newpos > pos) {
					l.pushstring(hints.substr(pos, newpos-pos)); {
						ret |= Traits::from_lua(l, -1, description);
					} l.pop();
				}
				pos = newpos+1;
			}
			return ret;
		}

		void window_hints_traits::to_lua(lua::state &l, uint16_t t, const std::string &)
		{
			std::string ret;
			for(auto i = Traits::map.begin(); i != Traits::map.end(); ++i) {
				if(i->second & t) {
					if(not ret.empty())
						ret += ", ";
					ret += i->first;
				}
			}
			l.pushstring(ret);
		}
	} /* namespace conky::priv */

	template<>
	lua_traits<alignment>::Map lua_traits<alignment>::map = {
		{ "top_left",      TOP_LEFT },
		{ "top_right",     TOP_RIGHT },
		{ "top_middle",    TOP_MIDDLE },
		{ "bottom_left",   BOTTOM_LEFT },
		{ "bottom_right",  BOTTOM_RIGHT },
		{ "bottom_middle", BOTTOM_MIDDLE },
		{ "middle_left",   MIDDLE_LEFT },
		{ "middle_middle", MIDDLE_MIDDLE },
		{ "middle_right",  MIDDLE_RIGHT },
		{ "none",          NONE }
	};

	template<>
	lua_traits<window_type>::Map lua_traits<window_type>::map = {
		{ "normal",   TYPE_NORMAL },
		{ "dock",     TYPE_DOCK },
		{ "panel",    TYPE_PANEL },
		{ "desktop",  TYPE_DESKTOP },
		{ "override", TYPE_OVERRIDE }
	};

	template<>
	lua_traits<window_hints>::Map lua_traits<window_hints>::map = {
		{ "undecorated",  HINT_UNDECORATED },
		{ "below",        HINT_BELOW },
		{ "above",        HINT_ABOVE },
		{ "sticky",       HINT_STICKY },
		{ "skip_taskbar", HINT_SKIP_TASKBAR },
		{ "skip_pager",   HINT_SKIP_PAGER }
	};

	namespace {
		// used to set the default value for own_window_title
		std::string gethostnamecxx()
		{ update_uname(); return info.uname_s.nodename; }
	}

	/*
	 * The order of these settings cannot be completely arbitrary. Some of them depend on others,
	 * and the setters are called in the order in which they are defined. The order should be:
	 * display_name -> out_to_x -> everything colour related
	 *                          -> border_*, own_window_*, etc -> own_window -> double_buffer ->  imlib_cache_size
	 */

	simple_config_setting<alignment>   text_alignment("alignment", BOTTOM_LEFT, false);
	conky::simple_config_setting<int>  gap_x("gap_x", 5, true);
	conky::simple_config_setting<int>  gap_y("gap_y", 60, true);

	simple_config_setting<std::string> display_name("display", std::string(), false);
	priv::out_to_x_setting                    out_to_x;

	range_config_setting<int>          border_inner_margin("border_inner_margin", 0,
														std::numeric_limits<int>::max(), 3, true);
	range_config_setting<int>          border_outer_margin("border_outer_margin", 0,
														std::numeric_limits<int>::max(), 1, true);
	range_config_setting<int>          border_width("border_width", 0,
														std::numeric_limits<int>::max(), 1, true);

	simple_config_setting<std::string> own_window_class("own_window_class",
																PACKAGE_NAME, false);

	simple_config_setting<std::string> own_window_title("own_window_title",
											PACKAGE_NAME " (" + gethostnamecxx()+")", false);

	simple_config_setting<window_type> own_window_type("own_window_type", TYPE_NORMAL, false);
	simple_config_setting<uint16_t, priv::window_hints_traits>
									   own_window_hints("own_window_hints", 0, false);


	fancy_x11_setting<bool>			   use_argb_visual("own_window_argb_visual", false,
																	&x11_output::set_visual);
	range_config_setting<float>        own_window_argb_value("own_window_argb_value",
																	0, 1, 1, false);

	fancy_x11_setting<bool>			   own_window("own_window", true, &x11_output::setup_window);

	double_buffer_setting			   double_buffer;

    fancy_x11_setting<bool>            use_xft("use_xft", false, &x11_output::setup_fonts);

	range_config_setting<char>		   stippled_borders("stippled_borders", 0,
												std::numeric_limits<char>::max(), 0, true);

	background_colour_setting          background_colour("own_window_colour", "black");
} /* namespace conky */


#ifdef BUILD_IMLIB2
/*
 * the only reason this is not in imlib2.cc is so that we can be sure it's setter executes after
 * use_xdbe
 */
imlib_cache_size_setting imlib_cache_size;
#endif
/******************** </SETTINGS> ************************/

#ifdef DEBUG
/* WARNING, this type not in Xlib spec */
static int __attribute__((noreturn)) x11_error_handler(Display *d, XErrorEvent *err)
{
	NORM_ERR("X Error: type %i Display %lx XID %li serial %lu error_code %i request_code %i minor_code %i other Display: %lx\n",
			err->type,
			(long unsigned)err->display,
			(long)err->resourceid,
			err->serial,
			err->error_code,
			err->request_code,
			err->minor_code,
			(long unsigned)d
			);
	abort();
}

static int __attribute__((noreturn)) x11_ioerror_handler(Display *d)
{
	NORM_ERR("X Error: Display %lx\n",
			(long unsigned)d
			);
	exit(1);
}
#endif /* DEBUG */

namespace {
	unsigned long colour_shift(unsigned long value, uint8_t need, uint8_t shift)
	{ return (value >> (16-need)) << shift; }
}

namespace conky {
	std::unique_ptr<x11_output::colour_factory>
	x11_output::colour_factory::create(Display &display, Visual &visual, Colormap colourmap)
	{
		switch(visual.c_class) {
			case TrueColor:
				return std::unique_ptr<colour_factory>(
						new true_colour_factory(display, visual, colourmap));
			default:
				return std::unique_ptr<colour_factory>(
						new alloc_colour_factory(display, colourmap));
		}
	}

	std::shared_ptr<x11_output::colour>
	x11_output::colour_factory::get_colour(const char *name, uint16_t alpha)
	{
		XColor exact, screen;
		if(XLookupColor(&display, colourmap, name, &exact, &screen) == 0) {
			throw new std::runtime_error(
					std::string("Unable to resolve colour name: `") + name + "'.");
		}
		return get_colour(screen, alpha);
	}

	std::shared_ptr<x11_output::colour>
	x11_output::colour_factory::get_colour(uint16_t red, uint16_t green, uint16_t blue,
											uint16_t alpha)
	{
		XColor colour;
		colour.red = red;
		colour.green = green;
		colour.blue = blue;
		return get_colour(colour, alpha);
	}

	std::shared_ptr<x11_output::colour>
	x11_output::true_colour_factory::get_colour(XColor &c, uint16_t alpha)
	{
		c.pixel = colour_shift(c.red, rgb_bits, red_shift) |
			colour_shift(c.green, rgb_bits, green_shift) |
			colour_shift(c.blue, rgb_bits, blue_shift) |
			( *use_argb_visual ? colour_shift(alpha, 8, 24) : 0u );

		return std::shared_ptr<colour>(new colour(c, alpha));
	}

	x11_output::alloc_colour_factory::alloc_colour_factory(Display &display_, Colormap colourmap_)
		: colour_factory(display_, colourmap_)
	{
		XColor w;
		w.red = w.green = w.blue = 0xffff;
		if(XAllocColor(&display, colourmap, &w) == 0)
			throw std::runtime_error("Unable to allocate any colours in the colourmap.");
		white.reset(new alloc_colour(w, *this));
	}

	std::shared_ptr<x11_output::colour>
	x11_output::alloc_colour_factory::get_colour(XColor &colour, uint16_t)
	{
		if(XAllocColor(&display, colourmap, &colour) == 0) {
			static bool warned = false;
			if(not warned) {
				warned = true;
				NORM_ERR(_("Failed to allocate colourmap entry for #%04x%04x%04x. "
							"All unallocated colours will be replaced by white."),
						colour.red, colour.green, colour.blue);
			}
			colour.red = colour.green = colour.blue = 0xffff;
			colour.pixel = white->get_pixel();
			return white;
		}
		return std::shared_ptr<x11_output::colour>(new alloc_colour(colour, *this));
	}

	class x11_output::window_handler: private non_copyable {
	protected:
		Display &display;
		const Window window;
		point size;
		point position;
		point text_pos;

	public:
		window_handler(Display &display_, Window window_)
			: display(display_), window(window_), size(1, 1)
		{ }

		virtual ~window_handler() { }

		Window get_window() { return window; }
		const point& get_text_pos() const { return text_pos; }
		const point& get_size() const { return size; }
		const point& get_position() const { return position; }
		virtual void resize(const point &size_) { size = size_; }
		virtual void move(const point &position_) { position = position_; }
		virtual void clear() = 0;
		virtual void handle_configure(const XConfigureEvent &) { }
		virtual void handle_reparent(const XReparentEvent &) { }
	};

	class x11_output::root_window_handler: public window_handler {
	public:
		root_window_handler(Display &display_, Window window_)
			: window_handler(display_, window_)
		{
			XWindowAttributes attrs;
			if (XGetWindowAttributes(&display, window, &attrs)) {
				size = { attrs.width, attrs.height };
			}
		}

		virtual void move(const point &position_) { text_pos = position_; }
		virtual void clear() { XClearArea(&display, window, 0, 0, size.x, size.y, False); }
	};

	class x11_output::own_window_handler: public window_handler {
		const int screen;
		bool fixed_size;
		bool fixed_pos;
		uint8_t pos_updates;
		enum { MAX_POS_UPDATES = 3 };

	public:
		own_window_handler(Display &display_, int screen_, Window window_)
			: window_handler(display_, window_), screen(screen_), fixed_size(false),
			  fixed_pos(false), pos_updates(0)
		{ }
		~own_window_handler() { XDestroyWindow(&display, window); }

		virtual void resize(const point &size_)
		{
			if(fixed_size || size_ == size)
				return;

			window_handler::resize(size_);
			XResizeWindow(&display, window, size.x, size.y);
			set_background();
		}

		virtual void move(const point &position_)
		{
			if(fixed_pos || position_ == position)
				return;

			if(pos_updates < MAX_POS_UPDATES)
				++pos_updates;
			XMoveWindow(&display, window, position_.x, position_.y);
			window_handler::move(position_);
		}

		virtual void clear() { XClearWindow(&display, window); }

		void set_background();
		virtual void handle_configure(const XConfigureEvent &e);
		virtual void handle_reparent(const XReparentEvent &) { set_background(); }
	};

	/* if no argb visual is configured sets background to ParentRelative for the Window and all
	 * parents, else real transparency is used */
	void x11_output::own_window_handler::set_background()
	{
		if(*use_argb_visual or *own_window_argb_value > 1e-3f)
			XSetWindowBackground(&display, window, background_colour->get()->get_pixel());
		else {
			Window parent = window;

			for (int i = 0; i < 50 && parent != RootWindow(&display, screen); i++) {
				Window r, *children;
				unsigned int n;

				XSetWindowBackgroundPixmap(&display, parent, ParentRelative);

				XQueryTree(&display, parent, &r, &parent, &children, &n);
				XFree(children);
			}
		}
	}

	void x11_output::own_window_handler::handle_configure(const XConfigureEvent &e)
	{
		point size_(e.width, e.height);
		if(size_ != size) {
			size = size_;
			fixed_size = true;
		}

		point position_(e.x, e.y);
		if(position_ != position) {
			position = position_;
			if(pos_updates >= MAX_POS_UPDATES)
				fixed_pos = true;
		}
	}

	class x11_output::buffer: private non_copyable {
	public:
		typedef std::function<void ()> DrawableChanged;

	private:
		Drawable drawable;
		std::shared_ptr<colour> foreground;

		static std::unique_ptr<buffer> try_xdbe(Display &display, window_handler &window);

	protected:
		Display &display;
		window_handler &window;
		GC gc;

		void change_gc(GC gc_, uint64_t value, unsigned long mask)
		{
			XGCValues values;
			switch(mask) {
				case GCLineWidth: values.line_width = value; break;
				case GCLineStyle: values.line_style = value; break;
				case GCDashList:  values.dashes     = value; break;
				default: assert(false);
			}
			XChangeGC(&display, gc_, mask, &values);
		}

		void change(uint64_t value, unsigned long mask)
		{ change_gc(gc, value, mask); }

		void set_drawable(Drawable drawable_) { drawable = drawable_; }

		buffer(Display &display_, window_handler &window_, Drawable drawable_)
			: drawable(drawable_), display(display_),  window(window_)
		{
			XGCValues values;
			values.graphics_exposures = 0;
			values.function = GXcopy;
			gc = XCreateGC(&display, drawable, GCFunction | GCGraphicsExposures, &values);
		}

	public:
		virtual ~buffer() { XFreeGC(&display, gc); }

		Drawable get_drawable() { return drawable; }
		GC get_gc() { return gc; }
		virtual buffer_type get_type() const = 0;
		virtual void clear() = 0;
		virtual void swap() = 0;
		virtual void resize(const point &size) { window.resize(size); }
		virtual const point& get_text_pos() { return window.get_text_pos(); }

		// should return false if we need a full redraw
		virtual bool expose(short x, short y, short width, short height) = 0;

		const std::shared_ptr<colour>& get_foreground() const { return foreground; }
		void set_foreground(const std::shared_ptr<colour> &c)
		{ foreground = c; XSetForeground(&display, gc, c->get_pixel()); }

		void set_dashes(char dashes) { change(dashes, GCDashList);}
		void set_line_style(int line_style) { change(line_style, GCLineStyle); }
		void set_line_width(unsigned short width) { change(width, GCLineWidth); }

		void draw_rectangle(point pos, const point &size)
		{
			pos += get_text_pos();
			XDrawRectangle(&display, drawable, gc, pos.x, pos.y, size.x, size.y);
		}

		static std::unique_ptr<buffer>
		create(buffer_type type, Display &display, window_handler &window, unsigned int depth);
	};

	class x11_output::single_buffer: public buffer {
	public:
		single_buffer(Display &display_, window_handler& window_)
			: buffer(display_, window_, window_.get_window())
		{ }

		virtual buffer_type get_type() const { return buffer_type::SINGLE; }
		virtual void clear() { window.clear(); }
		virtual void swap() { }
		virtual bool expose(short, short, short, short) { return false; }
	};

#ifdef BUILD_XDBE
	class x11_output::xdbe_buffer: public buffer {
		XdbeSwapInfo swapinfo;

	public:
		xdbe_buffer(Display &display_, window_handler& window_, XdbeBackBuffer back_buffer)
			: buffer(display_, window_, back_buffer)
		{ swapinfo.swap_window = window.get_window(); }

		virtual ~xdbe_buffer()
		{ XdbeDeallocateBackBufferName(&display, get_drawable()); }

		virtual buffer_type get_type() const { return buffer_type::XDBE; }

		virtual void swap()
		{
			swapinfo.swap_action = XdbeCopied;
			XdbeSwapBuffers(&display, &swapinfo, 1);
		}

		virtual void clear()
		{
			swapinfo.swap_action = XdbeBackground;
			XdbeSwapBuffers(&display, &swapinfo, 1);
		}

		virtual bool expose(short x, short y, short width, short height)
		{
			XCopyArea(&display, get_drawable(), window.get_window(), gc, x, y, width, height, x, y);
			return true;
		}
	};
#endif /*BUILD_XDBE*/

	std::unique_ptr<x11_output::buffer>
	x11_output::buffer::try_xdbe(Display &display, window_handler &window)
	{
#ifdef BUILD_XDBE
		int major, minor;

		if (not XdbeQueryExtension(&display, &major, &minor)) {
			NORM_ERR("No compatible double buffer extension found.");
			return {};
		}

		XdbeBackBuffer back_buffer = XdbeAllocateBackBufferName(&display,
				window.get_window(), XdbeBackground);
		if (back_buffer != None)
			return std::unique_ptr<buffer>(new xdbe_buffer(display, window, back_buffer));
		else {
			NORM_ERR("Failed to allocate back buffer. Falling back to pixmap buffer.");
			return {};
		}
#else
		NORM_ERR("XDBE support disabled during compilation. Will use pixmap buffer instead.");
		(void) display;
		(void) window;
		return {};
#endif
	}

	class x11_output::pixmap_buffer: public buffer {
		point size;
		unsigned int depth;
		Pixmap background;
		GC copy_gc;

		static const point dummy_pos;

		Pixmap create_pixmap()
		{ return XCreatePixmap(&display, window.get_window(), size.x, size.y, depth); }

		void semi_clear()
		{
			window.clear();
			const point &pos = window.get_text_pos();
			XCopyArea(&display, window.get_window(), background, copy_gc,
					pos.x, pos.y, size.x, size.y, 0, 0);
			Pixmap t = get_drawable();
			set_drawable(background);
			background = t;
		}

	public:
		pixmap_buffer(Display &display_, window_handler &window_, unsigned int depth_);

		virtual ~pixmap_buffer()
		{
			XFreePixmap(&display, get_drawable());
			XFreePixmap(&display, background);
			XFreeGC(&display, copy_gc);
		}

		virtual bool expose(short x, short y, short width, short height)
		{
			const point &pos = window.get_text_pos();
			XCopyArea(&display, get_drawable(), window.get_window(), copy_gc,
						x, y, width, height, x+pos.x, y+pos.y);
			return true;
		}

		virtual buffer_type get_type() const { return buffer_type::PIXMAP; }
		virtual void swap() { expose(0, 0, size.x, size.y); }
		virtual const point& get_text_pos() { return dummy_pos; }

		virtual void clear()
		{
			semi_clear();
			const point &pos = window.get_text_pos();
			XCopyArea(&display, background, window.get_window(), copy_gc,
					0, 0, size.x, size.y, pos.x, pos.y);
		}

		virtual void resize(const point &size_);
	};
	const point x11_output::pixmap_buffer::dummy_pos(0, 0);

	x11_output::pixmap_buffer::pixmap_buffer(Display &display_, window_handler &window_,
											 unsigned int depth_)
		: buffer(display_, window_, XCreatePixmap(&display_, window_.get_window(),
									window.get_size().x, window.get_size().y, depth_)),
		  size(window.get_size()), depth(depth_)
	{
		background = create_pixmap();

		XGCValues values;
		values.function = GXcopy;
		values.graphics_exposures = 0;
		copy_gc = XCreateGC(&display_, window.get_window(),
				GCFunction | GCGraphicsExposures, &values);

		semi_clear();
	}

	void x11_output::pixmap_buffer::resize(const point &size_)
	{
		if(size_ == size)
			return;

		XFreePixmap(&display, get_drawable());
		XFreePixmap(&display, background );

		size = size_;

		set_drawable(create_pixmap());
		background = create_pixmap();
		semi_clear();
	}

	std::unique_ptr<x11_output::buffer>
	x11_output::buffer::create(buffer_type type, Display &display, window_handler &window,
							   unsigned int depth) {

		std::unique_ptr<buffer> ret;
		const char *ctype;

		if(type == buffer_type::SINGLE) {
			ret.reset(new single_buffer(display, window));
			ctype = "single";
		} else {
			if(type == buffer_type::XDBE)
				ret = try_xdbe(display, window);
			if(ret)
				ctype = "XDBE double";
			else {
				ret.reset(new pixmap_buffer(display, window, depth));
				ctype = "pixmap double";
			}
		}
		fprintf(stderr, PACKAGE_NAME": drawing to %s buffer\n", ctype);
		return ret;
	}

	class x11_output::font: private non_copyable {
	public:
		virtual ~font() { }

		virtual point get_max_extents() = 0;
		virtual point get_text_size(const std::string &text) = 0;
		virtual void draw_text(const std::string &text, const point &pos, const point &size) = 0;
	};

	class x11_output::font_factory: private non_copyable {
	protected:
		Display &display;
		buffer &drawable;

		font_factory(Display &display_, buffer &drawable_)
			: display(display_), drawable(drawable_)
		{ }

		class load_font_error: public std::runtime_error {
		public:
			load_font_error(const std::string &message) : std::runtime_error(message) { }
		};

	public:
		virtual ~font_factory() { }
		virtual void drawable_changed() { }
		virtual std::shared_ptr<font> get_font(const char *name) = 0;
		virtual std::shared_ptr<font> get_default_font() = 0;
	};

	class x11_output::xlib_font_factory: public font_factory {
		class xlib_font: public font {
			xlib_font_factory &factory;
			XFontSet fontset;
			const XRectangle *font_extents;

		public:
			xlib_font(xlib_font_factory &factory_, const char *name);
			virtual ~xlib_font() { XFreeFontSet(&factory.display, fontset); }

			virtual point get_max_extents()
			{
				return { font_extents->width - font_extents->x,
						font_extents->height - font_extents->y};
			}

			virtual point get_text_size(const std::string &text)
			{
				XRectangle size;
				Xutf8TextExtents(fontset, text.c_str(), text.length(), NULL, &size);
				return { size.width, font_extents->height - font_extents->y };
			}

			virtual void
			draw_text(const std::string &text, const point &pos_, const point &)
			{
				point pos = pos_ + factory.drawable.get_text_pos();
				// TODO: clipping ?
				Xutf8DrawString(&factory.display, factory.drawable.get_drawable(), fontset,
						factory.drawable.get_gc(), pos.x, pos.y, text.c_str(), text.length());
			}
		};

	public:
		xlib_font_factory(Display &display_, buffer &drawable_)
			: font_factory(display_, drawable_)
		{ }

		virtual std::shared_ptr<font> get_font(const char *name)
		{
			try {
				return std::shared_ptr<font>(new xlib_font(*this, name));
			}
			catch(load_font_error &e) {
				NORM_ERR("%s Loading default font instead.", e.what());
				return get_default_font();
			}
		}

		virtual std::shared_ptr<font> get_default_font()
		{ return std::shared_ptr<font>(new xlib_font(*this, "fixed")); }
	};

	x11_output::xlib_font_factory::xlib_font::xlib_font(xlib_font_factory &factory_,
														const char *name)
		: factory(factory_)
	{
		char **missing_charset_list;
		int missing_charset_count;
		char *def_string;
		fontset = XCreateFontSet(&factory.display, name, &missing_charset_list,
				&missing_charset_count, &def_string);
		if(fontset == None) {
			throw load_font_error(std::string("Unable to create font set for font '") +
					name + "'.");
		}
		if(missing_charset_count > 0) {
			std::string charsets = '\'' + std::string(missing_charset_list[0]) + '\'';
			for(int i = 1; i < missing_charset_count; ++i)
				charsets += std::string(", '") + missing_charset_list[i] + '\'';
			XFreeStringList(missing_charset_list);
			NORM_ERR("Unable to load some character sets (%s) for font '%s'. "
					"Continuing, but missing characters will be replaced by '%s'.",
					charsets.c_str(), name, def_string);
		}
		font_extents = &(XExtentsOfFontSet(fontset)->max_logical_extent);
	}

#ifdef BUILD_XFT
	class x11_output::xft_font_factory: public font_factory {
		int screen;
		XftDraw *draw;

		class xft_font: public font {
			xft_font_factory &factory;
			XftFont *xf;

		public:
			xft_font(xft_font_factory &factory_, int screen, const char *name)
				: factory(factory_), xf(XftFontOpenName(&factory_.display, screen, name))
			{
				if(xf == NULL)
					throw load_font_error(std::string("Unable to load xft font '") + name + "'.");
			}

			~xft_font() { XftFontClose(&factory.display, xf); }

			virtual point get_max_extents() { return { xf->max_advance_width, xf->height }; }
			virtual point get_text_size(const std::string &text)
			{
				XGlyphInfo gi;
				XftTextExtentsUtf8(&factory.display, xf,
						reinterpret_cast<const FcChar8 *>(text.c_str()), text.length(), &gi);
				return { gi.xOff, xf->height };
			}

			virtual void draw_text(const std::string &text, const point &pos_, const point &)
			{
				point pos = pos_ + factory.drawable.get_text_pos();

				// XXX clipping?
				const auto &c = factory.drawable.get_foreground();
				const auto &xc = c->get_xcolor();
				XftColor xftc;

				xftc.pixel = xc.pixel;
				xftc.color.red = xc.red;
				xftc.color.green = xc.green;
				xftc.color.blue = xc.blue;
				xftc.color.alpha = c->get_alpha();

				XftDrawStringUtf8(factory.draw, &xftc, xf, pos.x, pos.y,
						reinterpret_cast<const FcChar8 *>(text.c_str()), text.length());
			}
		};

	public:
		xft_font_factory(Display &display_, int screen_, Visual &visual, Colormap colourmap,
				buffer &drawable_)
			: font_factory(display_, drawable_), screen(screen_),
			  draw(XftDrawCreate(&display_, drawable.get_drawable(), &visual, colourmap))
		{ }

		virtual ~xft_font_factory() { XftDrawDestroy(draw); }

		virtual void drawable_changed() { XftDrawChange(draw, drawable.get_drawable()); }

		virtual std::shared_ptr<font> get_font(const char *name)
		{
			try {
				return std::shared_ptr<font>(new xft_font(*this, screen, name));
			}
			catch(load_font_error &e) {
				NORM_ERR("%s Loading default font instead.", e.what());
				return get_default_font();
			}
		}

		virtual std::shared_ptr<font> get_default_font()
		{ return std::shared_ptr<font>(new xft_font(*this, screen, "courier-12")); }
	};
#endif /* BUILD_XFT */

	x11_output::x11_output(uint32_t period, const std::string &display_)
		: output_method(period, true), display(NULL), screen(0), root(0),
  		  desktop(0), visual(NULL), depth(0), colourmap(0), drawable(0)

	{
		// passing NULL to XOpenDisplay should open the default display
		const char *disp = display_.size() ? display_.c_str() : NULL;
		if ((display = XOpenDisplay(disp)) == NULL) {
			throw std::runtime_error(std::string("can't open display: ") + XDisplayName(disp));
		}

		screen = DefaultScreen(display);
		display_size = { DisplayWidth(display, screen), DisplayHeight(display, screen) };
		find_root_and_desktop_window();

#ifdef DEBUG
		_Xdebug = 1;
		/* WARNING, this type not in Xlib spec */
		XSetErrorHandler(&x11_error_handler);
		XSetIOErrorHandler(&x11_ioerror_handler);
#endif /* DEBUG */
	}

	x11_output::~x11_output()
	{
		fg_colour.reset();
		colours.reset();
		current_font.reset();
		fonts.reset();
		drawable.reset();
		window.reset();
		XCloseDisplay(display);
	}

	void x11_output::use_root_window()
	{
		fprintf(stderr, PACKAGE_NAME": drawing to desktop window\n");
		window.reset(new root_window_handler(*display, desktop));

		XSelectInput(display, window->get_window(), ExposureMask | PropertyChangeMask);
	}

	void x11_output::create_window(bool override) {
		XSetWindowAttributes attrs = { ParentRelative, 0L, 0, 0L, 0, 0,
			Always, 0L, 0L, False, StructureNotifyMask | ExposureMask, 0L,
			override, colourmap, 0 };
		if(not override)
			attrs.event_mask |= ButtonPressMask | ButtonReleaseMask;

		unsigned long flags = CWBorderPixel | CWColormap | CWOverrideRedirect | CWBackPixel;

		Window w = XCreateWindow(display, override ? desktop : root, 0, 0, 1, 1, 0, depth,
				InputOutput, visual, flags, &attrs);
		window.reset(new own_window_handler(*display, screen, w));

		XLowerWindow(display, window->get_window());
	}

	void x11_output::use_own_window() {
		if (*own_window_type == TYPE_OVERRIDE) {

			/* An override_redirect True window.
			 * No WM hints or button processing needed. */

			create_window(true);

			fprintf(stderr, PACKAGE_NAME": window type - override\n");
		} else { /* *own_window_type != TYPE_OVERRIDE */

			/* A window managed by the window manager.
			 * Process hints and buttons. */

			XClassHint classHint;
			XWMHints wmHint;
			Atom xa;

			create_window(false);

			// class_name must be a named local variable, so that c_str() remains valid until we
			// call XmbSetWMProperties(). We use const_cast because, for whatever reason,
			// res_name is not declared as const char *. XmbSetWMProperties hopefully doesn't
			// modify the value (hell, even their own example app assigns a literal string
			// constant to the field)
			const std::string &class_name = *own_window_class;

			classHint.res_name = const_cast<char *>(class_name.c_str());
			classHint.res_class = classHint.res_name;

			uint16_t hints = *own_window_hints;

			wmHint.flags = InputHint | StateHint;

			/* allow decorated windows to be given input focus by WM */
			wmHint.input = hints&HINT_UNDECORATED ? False : True;

			if (*own_window_type == TYPE_DOCK || *own_window_type == TYPE_PANEL) {
				wmHint.initial_state = WithdrawnState;
			} else {
				wmHint.initial_state = NormalState;
			}

			XmbSetWMProperties(display, window->get_window(), NULL, NULL, argv_copy,
					argc_copy, NULL, &wmHint, &classHint);
			XStoreName(display, window->get_window(), own_window_title->c_str() );

			/* Sets an empty WM_PROTOCOLS property */
			XSetWMProtocols(display, window->get_window(), NULL, 0);

			/* Set window type */
			if ((xa = ATOM(_NET_WM_WINDOW_TYPE)) != None) {
				Atom prop;

				switch (*own_window_type) {
					case TYPE_DESKTOP:
						prop = ATOM(_NET_WM_WINDOW_TYPE_DESKTOP);
						fprintf(stderr, PACKAGE_NAME": window type - desktop\n");
						break;
					case TYPE_DOCK:
						prop = ATOM(_NET_WM_WINDOW_TYPE_DOCK);
						fprintf(stderr, PACKAGE_NAME": window type - dock\n");
						break;
					case TYPE_PANEL:
						prop = ATOM(_NET_WM_WINDOW_TYPE_DOCK);
						fprintf(stderr, PACKAGE_NAME": window type - panel\n");
						break;
					case TYPE_NORMAL:
					default:
						prop = ATOM(_NET_WM_WINDOW_TYPE_NORMAL);
						fprintf(stderr, PACKAGE_NAME": window type - normal\n");
						break;
				}
				XChangeProperty(display, window->get_window(), xa, XA_ATOM, 32,
						PropModeReplace, (unsigned char *) &prop, 1);
			}

			/* Set desired hints */

			/* Window decorations */
			if(hints & HINT_UNDECORATED) {
				xa = ATOM(_MOTIF_WM_HINTS);
				if (xa != None) {
					long prop[5] = { 2, 0, 0, 0, 0 };
					XChangeProperty(display, window->get_window(), xa, xa, 32,
							PropModeReplace, (unsigned char *) prop, 5);
				}
			}

			/* Below other windows */
			if (hints & HINT_BELOW) {
				xa = ATOM(_WIN_LAYER);
				if (xa != None) {
					long prop = 0;

					XChangeProperty(display, window->get_window(), xa, XA_CARDINAL, 32,
							PropModeAppend, (unsigned char *) &prop, 1);
				}

				xa = ATOM(_NET_WM_STATE);
				if (xa != None) {
					Atom xa_prop = ATOM(_NET_WM_STATE_BELOW);

					XChangeProperty(display, window->get_window(), xa, XA_ATOM, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}
			}

			/* Above other windows */
			if (hints & HINT_ABOVE) {
				xa = ATOM(_WIN_LAYER);
				if (xa != None) {
					long prop = 6;

					XChangeProperty(display, window->get_window(), xa, XA_CARDINAL, 32,
							PropModeAppend, (unsigned char *) &prop, 1);
				}

				xa = ATOM(_NET_WM_STATE);
				if (xa != None) {
					Atom xa_prop = ATOM(_NET_WM_STATE_ABOVE);

					XChangeProperty(display, window->get_window(), xa, XA_ATOM, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}
			}

			/* Sticky */
			if (hints & HINT_STICKY) {
				xa = ATOM(_NET_WM_DESKTOP);
				if (xa != None) {
					CARD32 xa_prop = 0xFFFFFFFF;

					XChangeProperty(display, window->get_window(), xa, XA_CARDINAL, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}

				xa = ATOM(_NET_WM_STATE);
				if (xa != None) {
					Atom xa_prop = ATOM(_NET_WM_STATE_STICKY);

					XChangeProperty(display, window->get_window(), xa, XA_ATOM, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}
			}

			/* Skip taskbar */
			if (hints & HINT_SKIP_TASKBAR) {
				xa = ATOM(_NET_WM_STATE);
				if (xa != None) {
					Atom xa_prop = ATOM(_NET_WM_STATE_SKIP_TASKBAR);

					XChangeProperty(display, window->get_window(), xa, XA_ATOM, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}
			}

			/* Skip pager */
			if (hints & HINT_SKIP_PAGER) {

				xa = ATOM(_NET_WM_STATE);
				if (xa != None) {
					Atom xa_prop = ATOM(_NET_WM_STATE_SKIP_PAGER);

					XChangeProperty(display, window->get_window(), xa, XA_ATOM, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}
			}
		}

		fprintf(stderr, PACKAGE_NAME": drawing to created window (0x%lx)\n", window->get_window());

		XMapWindow(display, window->get_window());
		XSelectInput(display, window->get_window(), ExposureMask | PropertyChangeMask | StructureNotifyMask
				| ButtonPressMask | ButtonReleaseMask );
	}

	/* Find root window and desktop window.
	 * Return desktop window on success,
	 * and set root and desktop byref return values.
	 * Return 0 on failure. */
	void x11_output::find_root_and_desktop_window()
	{
		Atom type;
		int format;
		unsigned long nitems, bytes;
		unsigned int n;

		root = RootWindow(display, screen);
		Window win = root;
		Window troot, parent, *children;
		unsigned char *buf = NULL;

		/* some window managers set __SWM_VROOT to some child of root window */
		XQueryTree(display, root, &troot, &parent, &children, &n);
		for(unsigned int i = 0; i < n; i++) {
			if (XGetWindowProperty(display, children[i], ATOM(__SWM_VROOT), 0, 1,
						False, XA_WINDOW, &type, &format, &nitems, &bytes, &buf)
					== Success && type == XA_WINDOW) {

				win = *(Window *) buf;

				XFree(buf);
				XFree(children);

				fprintf(stderr,
						PACKAGE_NAME": desktop window (%lx) found from __SWM_VROOT property\n",
						win);

				root = win;
				desktop = win;
				return;
			}

			if (buf) {
				XFree(buf);
				buf = 0;
			}
		}
		XFree(children);

		/* get subwindows from root */
		desktop = find_subwindow(root);

		if (win != root) {
			fprintf(stderr,
					PACKAGE_NAME": desktop window (%lx) is subwindow of root window (%lx)\n",
					win, root);
		} else {
			fprintf(stderr, PACKAGE_NAME": desktop window (%lx) is root window\n", win);
		}
	}

	Window x11_output::find_subwindow(Window win)
	{
		unsigned int i, j;
		Window troot, parent, *children;
		unsigned int n;

		/* search subwindows with same size as display or work area */

		for (i = 0; i < 10; i++) {
			XQueryTree(display, win, &troot, &parent, &children, &n);

			for (j = n; j > 0; --j) {
				XWindowAttributes attrs;

				if (XGetWindowAttributes(display, children[j-1], &attrs)) {
					/* Window must be mapped and same size as display or
					 * work space */
					if (attrs.map_state != 0
							&& attrs.width == display_width && attrs.height == display_height) {
						win = children[j-1];
						break;
					}
				}
			}

			XFree(children);
			if (j == 0) {
				break;
			}
		}

		return win;
	}

	XColor x11_output::get_rgb(const std::shared_ptr<colour> &colour)
	{
		XColor c;
		c.pixel = colour->get_pixel();
		XQueryColor(display, colourmap, &c);
		return c;
	}

	bool x11_output::set_visual(bool argb)
	{
		if(argb) {
			/* code from gtk project, gdk_screen_get_rgba_visual */
			XVisualInfo visual_template;
			XVisualInfo *visual_list;
			int nxvisuals = 0, i;

			visual_template.screen = screen;
			visual_list = XGetVisualInfo (display, VisualScreenMask,
					&visual_template, &nxvisuals);
			for (i = 0; i < nxvisuals; i++) {
				if (visual_list[i].depth == 32 &&
						(visual_list[i].red_mask   == 0xff0000 &&
						 visual_list[i].green_mask == 0x00ff00 &&
						 visual_list[i].blue_mask  == 0x0000ff)) {

					visual = visual_list[i].visual;
					depth = visual_list[i].depth;
					fprintf(stderr, PACKAGE_NAME ": Found ARGB visual.\n");
					XFree(visual_list);

					colourmap = XCreateColormap(display,
							DefaultRootWindow(display), visual, AllocNone);
					return true;
				}
			}
			// no argb visual available
			fprintf(stderr, PACKAGE_NAME ": No ARGB visual found.\n");
			XFree(visual_list);
		}

		visual = DefaultVisual(display, screen);
		colourmap = DefaultColormap(display, screen);
		depth = DefaultDepth(display, screen);
		return false;
	}

	x11_output::buffer_type x11_output::setup_buffer(buffer_type type)
	{
		drawable = buffer::create(type, *display, *window, depth);
		colours = colour_factory::create(*display, *visual, colourmap);
		fg_colour = colours->get_colour("white");
		drawable->set_foreground(fg_colour);
		return drawable->get_type();
	}

	bool x11_output::setup_fonts(bool xft)
	{
		if(xft) {
#ifdef BUILD_XFT
			fonts = std::unique_ptr<font_factory>(new xft_font_factory(*display, screen, *visual,
													colourmap, *drawable));
#else
			NORM_ERR("Support for Xft fonts disabled during compilation. "
					"Will use xlib font API instead.");
#endif
		}
		if(not fonts) {
			fonts = std::unique_ptr<font_factory>(new xlib_font_factory(*display, *drawable));
			xft = false;
		}

		current_font = fonts->get_default_font();

		XFlush(display);
		return xft;
	}

	void x11_output::work()
	{
		while(true) {
			XFlush(display);
			fd_set set;
			FD_ZERO(&set);
			FD_SET(ConnectionNumber(display), &set);
			FD_SET(signalfd(), &set);
			int r = select(std::max(ConnectionNumber(display), signalfd())+1,
							&set, NULL, NULL, NULL);
			if(r == -1)
				throw errno_error("select", errno);
			if(is_done())
				return;

			bool need_redraw = false;
			if(FD_ISSET(signalfd(), &set)) {
				get_signal();
				need_redraw = true;
			}
			process_events(need_redraw);

			if(not need_redraw)
				continue;

			point size = get_global_text()->size(*this);
			int b = *border_inner_margin + *border_width + *border_outer_margin;
			size = max(equal_point(1), size + equal_point(2*b));

			window->resize(size);
			drawable->resize(size);

			point pos;
			const point &window_size = window->get_size();
			alignment align = *text_alignment;
			switch (align) {
				case TOP_LEFT: case TOP_RIGHT: case TOP_MIDDLE:
					pos.y = *gap_y;
					break;

				case BOTTOM_LEFT: case BOTTOM_RIGHT: case BOTTOM_MIDDLE: default:
					pos.y = display_size.y - window_size.y - *gap_y;
					break;

				case MIDDLE_LEFT: case MIDDLE_RIGHT: case MIDDLE_MIDDLE:
					pos.y = (display_size.y - window_size.y) / 2;
					break;
			}
			switch (align) {
				case TOP_LEFT: case BOTTOM_LEFT: case MIDDLE_LEFT: default:
					pos.x = *gap_x;
					break;

				case TOP_RIGHT: case BOTTOM_RIGHT: case MIDDLE_RIGHT:
					pos.x = display_size.x - window_size.x - *gap_x;
					break;

				case TOP_MIDDLE: case BOTTOM_MIDDLE: case MIDDLE_MIDDLE:
					pos.x = (display_size.x - window_size.x) / 2;
					break;
			}
			window->move(pos);

			drawable->clear();
			if(*border_width > 0) {
				if(*stippled_borders > 0) {
					drawable->set_dashes(*stippled_borders);
					drawable->set_line_style(LineOnOffDash);
				} else
					drawable->set_line_style(LineSolid);
				drawable->set_line_width(*border_width);
				drawable->draw_rectangle(equal_point(*border_outer_margin + *border_width/2),
						size - equal_point(*border_outer_margin*2+*border_width));
			}
			get_global_text()->draw(*this, point(b, b), size - point(2*b, 2*b));
			drawable->swap();
		}
	}

	void x11_output::process_events(bool &need_redraw)
	{
		point ul = window->get_size();
		point lr(0,0);

		while(XPending(display)) {
			XEvent ev;
			XNextEvent(display, &ev);
			switch(ev.type) {
				case ConfigureNotify:
					window->handle_configure(ev.xconfigure);
					fonts->drawable_changed();
					break;
				case ReparentNotify:
					window->handle_reparent(ev.xreparent);
					break;
				case Expose:
					XExposeEvent &eev = ev.xexpose;
					ul = min(ul, { eev.x, eev.y });
					lr = max(lr, { eev.x+eev.width, eev.y+eev.height } );
					break;
			}
		}

		if(!need_redraw and ul.x < lr.x)
			need_redraw = !drawable->expose(ul.x, ul.y, lr.x-ul.x, lr.y-ul.y);
	}

	point x11_output::get_max_extents() const
	{ return current_font->get_max_extents(); }

	point x11_output::get_text_size(const std::u32string &text) const
	{ return get_text_size(conv.to_utf8(text)); }

	point x11_output::get_text_size(const std::string &text) const
	{ return current_font->get_text_size(text); }

	void x11_output::draw_text(const std::u32string &text, const point &p, const point &size)
	{ draw_text(conv.to_utf8(text), p, size); }

	void x11_output::draw_text(const std::string &text, const point &p, const point &size)
	{ current_font->draw_text(text, p, size); }
}

//Get current desktop number
static inline void get_x11_desktop_current(Display *current_display, Window root, Atom atom)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *prop = NULL;
	struct information *current_info = &info;

	if (atom == None) return;

	if ( (XGetWindowProperty( current_display, root, atom,
					0, 1L, False, XA_CARDINAL,
					&actual_type, &actual_format, &nitems,
					&bytes_after, &prop ) == Success ) &&
			(actual_type == XA_CARDINAL) &&
			(nitems == 1L) && (actual_format == 32) ) {
		current_info->x11.desktop.current = prop[0]+1;
	}
	if(prop) {
		XFree(prop);
	}
}

//Get total number of available desktops
static inline void get_x11_desktop_number(Display *current_display, Window root, Atom atom)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *prop = NULL;
	struct information *current_info = &info;

	if (atom == None) return;

	if ( (XGetWindowProperty( current_display, root, atom,
					0, 1L, False, XA_CARDINAL,
					&actual_type, &actual_format, &nitems,
					&bytes_after, &prop ) == Success ) &&
			(actual_type == XA_CARDINAL) &&
			(nitems == 1L) && (actual_format == 32) ) {
		current_info->x11.desktop.number = prop[0];
	}
	if(prop) {
		XFree(prop);
	}
}

//Get all desktop names
static inline void get_x11_desktop_names(Display *current_display, Window root, Atom atom)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *prop = NULL;
	struct information *current_info = &info;

	if (atom == None) return;

	if ( (XGetWindowProperty( current_display, root, atom,
					0, (~0L), False, ATOM(UTF8_STRING),
					&actual_type, &actual_format, &nitems,
					&bytes_after, &prop ) == Success ) &&
			(actual_type == ATOM(UTF8_STRING)) &&
			(nitems > 0L) && (actual_format == 8) ) {

		current_info->x11.desktop.all_names.assign(reinterpret_cast<const char *>(prop), nitems);
	}
	if(prop) {
		XFree(prop);
	}
}

//Get current desktop name
static inline void get_x11_desktop_current_name(const std::string &names)
{
	struct information *current_info = &info;
	unsigned int i = 0, j = 0;
	int k = 0;

	while ( i < names.size() ) {
		if ( names[i++] == '\0' ) {
			if ( ++k == current_info->x11.desktop.current ) {
				current_info->x11.desktop.name.assign(names.c_str()+j);
				break;
			}
			j = i;
		}
	}
}

void get_x11_desktop_info(Display *current_display, Atom atom)
{
	Window root;
	static Atom atom_current, atom_number, atom_names;
	struct information *current_info = &info;
	XWindowAttributes window_attributes;

	root = RootWindow(current_display, current_info->x11.monitor.current);

	/* Check if we initialise else retrieve changed property */
	if (atom == 0) {
		atom_current = XInternAtom(current_display, "_NET_CURRENT_DESKTOP", True);
		atom_number  = XInternAtom(current_display, "_NET_NUMBER_OF_DESKTOPS", True);
		atom_names   = XInternAtom(current_display, "_NET_DESKTOP_NAMES", True);
		get_x11_desktop_current(current_display, root, atom_current);
		get_x11_desktop_number(current_display, root, atom_number);
		get_x11_desktop_names(current_display, root, atom_names);
		get_x11_desktop_current_name(current_info->x11.desktop.all_names);

		/* Set the PropertyChangeMask on the root window, if not set */
		XGetWindowAttributes(display, root, &window_attributes);
		if (!(window_attributes.your_event_mask & PropertyChangeMask)) {
			XSetWindowAttributes attributes;
			attributes.event_mask = window_attributes.your_event_mask | PropertyChangeMask;
			XChangeWindowAttributes(display, root, CWEventMask, &attributes);
			XGetWindowAttributes(display, root, &window_attributes);
		}
	} else {
		if (atom == atom_current) {
			get_x11_desktop_current(current_display, root, atom_current);
			get_x11_desktop_current_name(current_info->x11.desktop.all_names);
		} else if (atom == atom_number) {
			get_x11_desktop_number(current_display, root, atom_number);
		} else if (atom == atom_names) {
			get_x11_desktop_names(current_display, root, atom_names);
			get_x11_desktop_current_name(current_info->x11.desktop.all_names);
		}
	}
}

static const char NOT_IN_X[] = "Not running in X";

void print_monitor(struct text_object *obj, char *p, int p_max_size)
{
	(void)obj;

	if(not *out_to_x) {
		strncpy(p, NOT_IN_X, p_max_size);
		return;
	}
	snprintf(p, p_max_size, "%d", XDefaultScreen(display));
}

void print_monitor_number(struct text_object *obj, char *p, int p_max_size)
{
	(void)obj;

	if(not *out_to_x) {
		strncpy(p, NOT_IN_X, p_max_size);
		return;
	}
	snprintf(p, p_max_size, "%d", XScreenCount(display));
}

void print_desktop(struct text_object *obj, char *p, int p_max_size)
{
	(void)obj;

	if(not *out_to_x) {
		strncpy(p, NOT_IN_X, p_max_size);
		return;
	}
	snprintf(p, p_max_size, "%d", info.x11.desktop.current);
}

void print_desktop_number(struct text_object *obj, char *p, int p_max_size)
{
	(void)obj;

	if(not *out_to_x) {
		strncpy(p, NOT_IN_X, p_max_size);
		return;
	}
	snprintf(p, p_max_size, "%d", info.x11.desktop.number);
}

void print_desktop_name(struct text_object *obj, char *p, int p_max_size)
{
	(void)obj;

	if(not *out_to_x) {
		strncpy(p, NOT_IN_X, p_max_size);
	} else {
		strncpy(p, info.x11.desktop.name.c_str(), p_max_size);
	}
}

#ifdef OWN_WINDOW
/* reserve window manager space */
void set_struts(int sidenum)
{
	Atom strut;
	if ((strut = ATOM(_NET_WM_STRUT)) != None) {
		/* reserve space at left, right, top, bottom */
		signed long sizes[12] = {0};
		int i;

		/* define strut depth */
		switch (sidenum) {
			case 0:
				/* left side */
				sizes[0] = window.x + window.width;
				break;
			case 1:
				/* right side */
				sizes[1] = display_width - window.x;
				break;
			case 2:
				/* top side */
				sizes[2] = window.y + window.height;
				break;
			case 3:
				/* bottom side */
				sizes[3] = display_height - window.y;
				break;
		}

		/* define partial strut length */
		if (sidenum <= 1) {
			sizes[4 + (sidenum*2)] = window.y;
			sizes[5 + (sidenum*2)] = window.y + window.height;
		} else if (sidenum <= 3) {
			sizes[4 + (sidenum*2)] = window.x;
			sizes[5 + (sidenum*2)] = window.x + window.width;
		}

		/* check constraints */
		for (i = 0; i < 12; i++) {
			if (sizes[i] < 0) {
				sizes[i] = 0;
			} else {
				if (i <= 1 || i >= 8) {
					if (sizes[i] > display_width) {
						sizes[i] = display_width;
					}
				} else {
					if (sizes[i] > display_height) {
						sizes[i] = display_height;
					}
				}
			}
		}

		XChangeProperty(display, window.window, strut, XA_CARDINAL, 32,
				PropModeReplace, (unsigned char *) &sizes, 4);

		if ((strut = ATOM(_NET_WM_STRUT_PARTIAL)) != None) {
			XChangeProperty(display, window.window, strut, XA_CARDINAL, 32,
					PropModeReplace, (unsigned char *) &sizes, 12);
		}
	}
}
#endif /* OWN_WINDOW */
