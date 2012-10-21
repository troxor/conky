/* -*- mode: c++; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: t -*-
 * vim: ts=4 sw=4 noet ai cindent syntax=cpp
 *
 * Conky, a system monitor, based on torsmo
 *
 * Please see COPYING for details
 *
 * Copyright (C) 2012 Pavel Labath et al.
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

#include <config.h>

#include "text-output.hh"

#include <algorithm>
#include <iostream>

#include "setting.hh"

#include "conky.h"

namespace conky {

	namespace {
		class text_output_setting: public simple_config_setting<bool> {
			typedef simple_config_setting<bool> Base;

			thread_handle<text_output> om;
		public:
			text_output_setting(const std::string &name_, bool default_value_)
				: Base(name_, default_value_, false)
			{}

			virtual const bool set(const bool &r, bool init)
			{
				assert(init);

				if(r)
					om = output_methods.register_thread<text_output>(1);

				return value = r;
			}

			virtual void cleanup()
			{ om.reset(); }

			const thread_handle<text_output>& get_om()
			{ return om; }
		};
				
		text_output_setting out_to_console("out_to_console",
				// Default value is false, unless we are building without X
#ifdef BUILD_X11
				false
#else
				true
#endif
				);
	}

	text_output::text_output(uint32_t period)
		: output_method(period, false), grid(25, std::u32string(80, ' '))
	{}

	void text_output::draw_text(const std::u32string &text, const point &p, const point &size)
	{
		if(p.y>=0 && p.y<static_cast<ssize_t>(grid.size())
				&& p.x<static_cast<ssize_t>(grid[p.y].size()) && size.y>0 && size.x>0) {


			auto tbegin = text.begin();
			auto gbegin = grid[p.y].begin();
			if(p.x < 0)
				tbegin -= p.x;
			else
				gbegin += p.x;

			size_t n = std::min(grid[p.y].end()-gbegin,
					std::min(text.end()-tbegin, static_cast<ssize_t>(size.x)));
			std::copy_n(tbegin, n, gbegin);
		}
	}

	void text_output::work()
	{
		get_global_text()->size(*this);
		get_global_text()->draw(*this, point(0, 0), point(80, 25));
		for(auto y = grid.begin(); y != grid.end(); ++y)
			std::cout << conv.to_utf8(*y) << std::endl;
	}
}
