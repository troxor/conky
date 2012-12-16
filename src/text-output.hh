/* -*- mode: c++; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: t -*-
 * vim: ts=4 sw=4 noet ai cindent syntax=cpp
 *
 * Conky, a system monitor, based on torsmo
 *
 * Please see COPYING for details
 *
 * Copyright (C) 2010 Pavel Labath et al.
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

#ifndef TEXT_OUTPUT_HH
#define TEXT_OUTPUT_HH

#include <vector>

#include "output-method.hh"
#include "unicode.hh"

namespace conky {

	class text_output: public output_method {
		unicode_converter conv;

		std::vector<std::u32string> grid;

	protected:
		virtual void work();

	public:
		text_output(uint32_t period);

		virtual point get_max_extents() const { return {1, 1}; }

		virtual point get_text_size(const std::u32string &text) const
		{ return point(text.length(), 1); }

		virtual point get_text_size(const std::string &text) const
		{ return get_text_size(conv.to_utf32(text)); }

		virtual void draw_text(const std::u32string &text, const point &p, const point &size);

		virtual void draw_text(const std::string &text, const point &p, const point &size)
		{ draw_text(conv.to_utf32(text), p, size); }
	};
}

#endif /* TEXT_OUTPUT_HH */
