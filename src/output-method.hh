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

#ifndef OUTPUT_METHOD_HH
#define OUTPUT_METHOD_HH

namespace conky {

	struct point {
		int32_t x;
		int32_t y;

		point()
			: x(0), y(0)
		{}

		point(int32_t x_, int32_t y_)
			: x(x_), y(y_)
		{}
	};

	inline point operator+(const point &l, const point &r)
	{ return { l.x+r.x, l.y+r.y }; }

	inline point operator-(const point &l, const point &r)
	{ return { l.x-r.x, l.y-r.y }; }

	inline point min(const point &l, const point &r)
	{ return { std::min(l.x, r.x), std::min(l.y, r.y) }; }

	class output_method {
	public:
		virtual ~output_method() {}

		virtual point get_text_size(const std::string &text) const = 0;
		virtual void draw_text(const std::string &text, const point &p, const point &size) = 0;
	};

}

#endif /* OUTPUT_METHOD_HH */
