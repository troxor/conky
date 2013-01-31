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

#include <config.h>

#include "fixed-layout.hh"

#include "logging.h"
#include "lua-traits.hh"

namespace conky {

	fixed_layout::item_info fixed_layout::read_item(lua::state &l, size_t itemno)
	{
		lua::stack_sentry s(l, -1);
		l.checkstack(1);

		item_info info;
		try {
			info.pos = lua_traits<point>::from_lua(l, -1, strprintf("item %zd", itemno));
		}
		catch(conversion_error &e) {
			NORM_ERR("fixed_layout: %s", e.what());
		}

		try {
			l.rawgeti(-1, 3);
			info.item = layout_item::create(l);
		}
		catch(std::runtime_error &e) {
			NORM_ERR("fixed_layout: Item %zd invalid.\n%s", itemno, e.what());
		}
		return std::move(info);
	}

	fixed_layout::fixed_layout(lua::state &l)
		: Base(l)
	{
		lua::stack_sentry s(l, -1);
		l.checkstack(1);

		try {
			lua::stack_sentry s2(l);
			l.rawgetfield(-1, "size"); {
				size_ = lua_traits<point>::from_lua(l, -1, "size of table layout");
			}
		}
		catch(conversion_error &e) {
			NORM_ERR("%s Using default.", e.what());
		}

		for(size_t i = 1; l.rawgeti(-1, i), !l.isnil(-1); ++i) {
			items.push_back(read_item(l, i));
		} l.pop();
	}

	point fixed_layout::size(output_method &om, std::vector<point> &data)
	{
		if(size_ != point(0, 0))
			return size_;

		point res;

		for(size_t i = 0; i < items.size(); ++i) {
			data[i] = items[i].item->size(om);
			res = max(res, items[i].pos + data[i]);
		}
		return res;
	}

	void
	fixed_layout::draw(output_method &om, const point &p, const point &size,
			std::vector<point> &data)
	{
		for(size_t i = 0; i < items.size(); ++i) {
			const auto &t = items[i];
			t.item->draw(om, p + t.pos, min(data[i], size-t.pos));
		}
	}
}
