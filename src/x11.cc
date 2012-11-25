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

#ifdef BUILD_ARGB
bool have_argb_visual;
#endif /* BUILD_ARGB */

// XXX: TEMPORARY stub variables
Display *display;
int display_width;
int display_height;
int screen;


/* Window stuff */
struct conky_window window;

/********************* <SETTINGS> ************************/
namespace conky {
	namespace priv {
		const bool out_to_x_setting::set(const bool &r, bool init)
		{
			assert(init);

			if(r)
				om = output_methods.register_thread<x11_output>(1, *display_name);

			return value = r;
		}

		const bool use_argb_visual_setting::set(const bool &r, bool init)
		{
			assert(init);

			if(*out_to_x)
				value = out_to_x.get_om()->set_visual(r);
			else
				value = false;

			return value;
		}

		const bool own_window_setting::set(const bool &r, bool init)
		{
			assert(init);
			if(*out_to_x) {
				if(r)
					out_to_x.get_om()->use_own_window();
				else
					out_to_x.get_om()->use_root_window();
				value = r;
			} else
				value = false;

			return value;
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
} /* namespace conky */

namespace priv {
#ifdef BUILD_XDBE
	bool use_xdbe_setting::set_up()
	{
		// double_buffer makes no sense when not drawing to X
		if(not *out_to_x)
			return false;

		int major, minor;

		if (not XdbeQueryExtension(display, &major, &minor)) {
			NORM_ERR("No compatible double buffer extension found");
			return false;
		}
		
		window.back_buffer = XdbeAllocateBackBufferName(display,
				window.window, XdbeBackground);
		if (window.back_buffer != None) {
			window.drawable = window.back_buffer;
		} else {
			NORM_ERR("Failed to allocate back buffer");
			return false;
		}

		XFlush(display);
		return true;
	}

	const bool use_xdbe_setting::set(const bool &r, bool init)
	{
		if(init) {
			if(r && set_up())
				value = true;
			else
				value = false;

			fprintf(stderr, PACKAGE_NAME": drawing to %s buffer\n",
					value?"double":"single");
		}
		return value;
	}



#else
	bool use_xpmdb_setting::set_up()
	{
		// double_buffer makes no sense when not drawing to X
		if(not *out_to_x)
			return false;

		window.back_buffer = XCreatePixmap(display,
				window.window, window.width+1, window.height+1, DefaultDepth(display, screen));
		if (window.back_buffer != None) {
// XXX			window.drawable = window.back_buffer;
		} else {
			NORM_ERR("Failed to allocate back buffer");
			return false;
		}
		

		XFlush(display);
		return true;
	}

	const bool use_xpmdb_setting::set(const bool &r, bool init)
	{
		if(init) {
			if(r && set_up())
				value = true;
			else
				value = false;

			fprintf(stderr, PACKAGE_NAME": drawing to %s buffer\n",
					value?"double":"single");
		}
		return value;
	}

	unsigned long colour_traits::from_lua(lua::state &/*l*/, int /*index*/, const std::string &)
	{
		return 0;
//		return *out_to_x ? get_x11_color(l.tostring(index)) : 0;
	}
#endif
}

template<>
conky::lua_traits<alignment>::Map conky::lua_traits<alignment>::map = {
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
conky::lua_traits<window_type>::Map conky::lua_traits<window_type>::map = {
	{ "normal",   TYPE_NORMAL },
	{ "dock",     TYPE_DOCK },
	{ "panel",    TYPE_PANEL },
	{ "desktop",  TYPE_DESKTOP },
	{ "override", TYPE_OVERRIDE }
};

template<>
conky::lua_traits<window_hints>::Map conky::lua_traits<window_hints>::map = {
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
 * The order of these settings cannot be completely arbitrary. Some of them depend on others, and
 * the setters are called in the order in which they are defined. The order should be:
 * display_name -> out_to_x -> everything colour related
 *                          -> border_*, own_window_*, etc -> own_window -> double_buffer ->  imlib_cache_size
 */

conky::simple_config_setting<alignment>   text_alignment("alignment", BOTTOM_LEFT, false);
conky::simple_config_setting<std::string> display_name("display", std::string(), false);
conky::priv::out_to_x_setting                    out_to_x;

priv::colour_setting					  color[10] = {
	{ "color0", 0xffffff },
	{ "color1", 0xffffff },
	{ "color2", 0xffffff },
	{ "color3", 0xffffff },
	{ "color4", 0xffffff },
	{ "color5", 0xffffff },
	{ "color6", 0xffffff },
	{ "color7", 0xffffff },
	{ "color8", 0xffffff },
	{ "color9", 0xffffff }
};
priv::colour_setting					  default_color("default_color", 0xffffff);
priv::colour_setting					  default_shade_color("default_shade_color", 0x000000);
priv::colour_setting					  default_outline_color("default_outline_color", 0x000000);

conky::range_config_setting<int>          border_inner_margin("border_inner_margin", 0,
													std::numeric_limits<int>::max(), 3, true);
conky::range_config_setting<int>          border_outer_margin("border_outer_margin", 0,
													std::numeric_limits<int>::max(), 1, true);
conky::range_config_setting<int>          border_width("border_width", 0,
													std::numeric_limits<int>::max(), 1, true);
#ifdef BUILD_XFT
conky::simple_config_setting<bool>        use_xft("use_xft", false, false);
#endif

conky::simple_config_setting<bool>        set_transparent("own_window_transparent", false, false);
conky::simple_config_setting<std::string> own_window_class("own_window_class",
															PACKAGE_NAME, false);

conky::simple_config_setting<std::string> own_window_title("own_window_title",
										PACKAGE_NAME " (" + gethostnamecxx()+")", false);

conky::simple_config_setting<window_type> own_window_type("own_window_type", TYPE_NORMAL, false);
conky::simple_config_setting<uint16_t, conky::priv::window_hints_traits>
									      own_window_hints("own_window_hints", 0, false);

priv::colour_setting                      background_colour("own_window_colour", 0);

conky::priv::use_argb_visual_setting      use_argb_visual;
#ifdef BUILD_ARGB
conky::range_config_setting<int>          own_window_argb_value("own_window_argb_value",
																0, 255, 255, false);
#endif
conky::priv::own_window_setting			  own_window;

#ifdef BUILD_XDBE
priv::use_xdbe_setting			 		  use_xdbe;
#else
priv::use_xpmdb_setting			 		  use_xpmdb;
#endif

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

namespace conky {
	x11_output::x11_output(uint32_t period, const std::string &display_)
		: output_method(period, false), display(NULL), screen(0), window(0), root(0),
		  desktop(0), visual(NULL), depth(0), colourmap(0), drawable(0), gc(0), fontset(NULL)
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

	void x11_output::use_root_window()
	{
		XWindowAttributes attrs;

		if (XGetWindowAttributes(display, desktop, &attrs)) {
			window_size = { attrs.width, attrs.height };
		}

		fprintf(stderr, PACKAGE_NAME": drawing to desktop window\n");
		drawable = desktop;

		XSelectInput(display, desktop, ExposureMask | PropertyChangeMask);

		create_gc();
	}

	void x11_output::create_window(bool override) {
		XSetWindowAttributes attrs = { ParentRelative, 0L, 0, 0L, 0, 0,
			Always, 0L, 0L, False, StructureNotifyMask | ExposureMask, 0L,
			override, colourmap, 0 };
		if(not override)
			attrs.event_mask |= ButtonPressMask | ButtonReleaseMask;

		unsigned long flags = CWBorderPixel | CWColormap | CWOverrideRedirect | CWBackPixel;
		int b = *border_inner_margin + *border_width
			+ *border_outer_margin;

		window = XCreateWindow(display, override ? desktop : root, 0, 0, b, b, 0, depth,
				InputOutput, visual, flags, &attrs);

		XLowerWindow(display, window);
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

			XmbSetWMProperties(display, window, NULL, NULL, argv_copy,
					argc_copy, NULL, &wmHint, &classHint);
			XStoreName(display, window, own_window_title->c_str() );

			/* Sets an empty WM_PROTOCOLS property */
			XSetWMProtocols(display, window, NULL, 0);

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
				XChangeProperty(display, window, xa, XA_ATOM, 32,
						PropModeReplace, (unsigned char *) &prop, 1);
			}

			/* Set desired hints */

			/* Window decorations */
			if(hints & HINT_UNDECORATED) {
				xa = ATOM(_MOTIF_WM_HINTS);
				if (xa != None) {
					long prop[5] = { 2, 0, 0, 0, 0 };
					XChangeProperty(display, window, xa, xa, 32,
							PropModeReplace, (unsigned char *) prop, 5);
				}
			}

			/* Below other windows */
			if (hints & HINT_BELOW) {
				xa = ATOM(_WIN_LAYER);
				if (xa != None) {
					long prop = 0;

					XChangeProperty(display, window, xa, XA_CARDINAL, 32,
							PropModeAppend, (unsigned char *) &prop, 1);
				}

				xa = ATOM(_NET_WM_STATE);
				if (xa != None) {
					Atom xa_prop = ATOM(_NET_WM_STATE_BELOW);

					XChangeProperty(display, window, xa, XA_ATOM, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}
			}

			/* Above other windows */
			if (hints & HINT_ABOVE) {
				xa = ATOM(_WIN_LAYER);
				if (xa != None) {
					long prop = 6;

					XChangeProperty(display, window, xa, XA_CARDINAL, 32,
							PropModeAppend, (unsigned char *) &prop, 1);
				}

				xa = ATOM(_NET_WM_STATE);
				if (xa != None) {
					Atom xa_prop = ATOM(_NET_WM_STATE_ABOVE);

					XChangeProperty(display, window, xa, XA_ATOM, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}
			}

			/* Sticky */
			if (hints & HINT_STICKY) {
				xa = ATOM(_NET_WM_DESKTOP);
				if (xa != None) {
					CARD32 xa_prop = 0xFFFFFFFF;

					XChangeProperty(display, window, xa, XA_CARDINAL, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}

				xa = ATOM(_NET_WM_STATE);
				if (xa != None) {
					Atom xa_prop = ATOM(_NET_WM_STATE_STICKY);

					XChangeProperty(display, window, xa, XA_ATOM, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}
			}

			/* Skip taskbar */
			if (hints & HINT_SKIP_TASKBAR) {
				xa = ATOM(_NET_WM_STATE);
				if (xa != None) {
					Atom xa_prop = ATOM(_NET_WM_STATE_SKIP_TASKBAR);

					XChangeProperty(display, window, xa, XA_ATOM, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}
			}

			/* Skip pager */
			if (hints & HINT_SKIP_PAGER) {

				xa = ATOM(_NET_WM_STATE);
				if (xa != None) {
					Atom xa_prop = ATOM(_NET_WM_STATE_SKIP_PAGER);

					XChangeProperty(display, window, xa, XA_ATOM, 32,
							PropModeAppend, (unsigned char *) &xa_prop, 1);
				}
			}
		}

		fprintf(stderr, PACKAGE_NAME": drawing to created window (0x%lx)\n", window);

		XMapWindow(display, window);


		/* Drawable is same as window. This may be changed by double buffering. */
		drawable = window;

		XSelectInput(display, window, ExposureMask | PropertyChangeMask | StructureNotifyMask
				| ButtonPressMask | ButtonReleaseMask );

		create_gc();
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

	bool x11_output::set_visual(bool argb) {
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
		depth = CopyFromParent;
		visual = CopyFromParent;
		return false;
	}

	void x11_output::create_gc()
	{
		XGCValues values;

		values.graphics_exposures = 0;
		values.function = GXcopy;
		gc = XCreateGC(display, drawable,
				GCFunction | GCGraphicsExposures, &values);

		char **missing_charset_list;
		int missing_charset_count;
		char *def_string;
		fontset = XCreateFontSet(display, "fixed", &missing_charset_list,
				&missing_charset_count, &def_string);
		if(fontset == NULL)
			throw std::runtime_error("Unable to create font set for font 'fixed'.");
		if(missing_charset_count > 0) {
			std::string charsets = '\'' + std::string(missing_charset_list[0]) + '\'';
			for(int i = 1; i < missing_charset_count; ++i)
				charsets += std::string(", '") + missing_charset_list[i] + '\'';
			XFreeStringList(missing_charset_list);
			NORM_ERR("Unable to load some character sets (%s) for font 'fixed'. "
					"Continuing, but missing characters will be replaced by '%s'.",
					charsets.c_str(), def_string);
		}

		XFlush(display);
	}

	void x11_output::work()
	{
		point size = get_global_text()->size(*this);
		if(window)
			XResizeWindow(display, window, size.x, size.y);
		get_global_text()->draw(*this, point(0, 0), size);
		XFlush(display);
	}

	point x11_output::get_text_size(const std::u32string &text) const
	{ return get_text_size(conv.to_utf8(text)); }
	point x11_output::get_text_size(const std::string &) const
	{ return { 100, 100 }; }
	void x11_output::draw_text(const std::string &, const point &, const point &) {}
	void x11_output::draw_text(const std::u32string &, const point &, const point &) {}
}

#ifdef OWN_WINDOW
namespace {
	/* helper function for set_transparent_background() */
	void do_set_background(Window win, int argb)
	{
		unsigned long colour = *background_colour | (argb<<24);
		XSetWindowBackground(display, win, colour);
	}
}

/* if no argb visual is configured sets background to ParentRelative for the Window and all parents,
   else real transparency is used */
void set_transparent_background(Window win)
{
#ifdef BUILD_ARGB
	if (have_argb_visual) {
		// real transparency
		do_set_background(win, *set_transparent ? 0 : *own_window_argb_value);
	} else {
#endif /* BUILD_ARGB */
	// pseudo transparency
	
	if (*set_transparent) {
		Window parent = win;
		unsigned int i;

		for (i = 0; i < 50 && parent != RootWindow(display, screen); i++) {
			Window r, *children;
			unsigned int n;

			XSetWindowBackgroundPixmap(display, parent, ParentRelative);

			XQueryTree(display, parent, &r, &parent, &children, &n);
			XFree(children);
		}
	} else
		do_set_background(win, 0);
#ifdef BUILD_ARGB
	}
#endif /* BUILD_ARGB */
}
#endif

void destroy_window(void)
{
#ifdef BUILD_XFT
	if(window.xftdraw) {
		XftDrawDestroy(window.xftdraw);
	}
#endif /* BUILD_XFT */
	memset(&window, 0, sizeof(struct conky_window));
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

#ifdef BUILD_XDBE
void xdbe_swap_buffers(void)
{
	if (*use_xdbe) {
		XdbeSwapInfo swap;

		swap.swap_window = window.window;
		swap.swap_action = XdbeBackground;
		XdbeSwapBuffers(display, &swap, 1);
	}
}
#else
void xpmdb_swap_buffers(void)
{
	if (*use_xpmdb) {
		XCopyArea(display, window.back_buffer, window.window, window.gc, 0, 0, window.width, window.height, 0, 0);
		XSetForeground(display, window.gc, 0);
// XXX		XFillRectangle(display, window.drawable, window.gc, 0, 0, window.width, window.height);
		XFlush(display);
	}
}
#endif /* BUILD_XDBE */
