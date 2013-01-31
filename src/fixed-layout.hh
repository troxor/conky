/* -*- mode: c++; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: t -*-
 * vim: ts=4 sw=4 noet ai cindent syntax=cpp
 *
 * Conky, a system monitor, based on torsmo
 *
 * Please see COPYING for details
 *
 * Copyright (C) 2013 Pavel Labath et al.
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

#ifndef FIXED_LAYOUT_HH
#define FIXED_LAYOUT_HH

#include <map>
#include <vector>

#include "list_map.hh"
#include "layout-engine.hh"
#include "luamm.hh"

namespace conky {

	class fixed_layout: public layout_engine<std::vector<point>> {
		typedef layout_engine<std::vector<point>> Base;

		struct item_info {
			point pos;
			std::shared_ptr<layout_item> item;
		};

		std::vector<item_info> items;
		point size_;

		item_info read_item(lua::state &l, size_t itemno);

	protected:
		virtual std::vector<point> make_data() { return std::vector<point>(items.size()); }
		virtual point size(output_method &om, std::vector<point> &data);
		virtual void
		draw(output_method &om, const point &p, const point &size, std::vector<point> &data);

	public:
		fixed_layout(lua::state &l);
	};
}

#endif /* FIXED_LAYOUT_HH */
