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

#include <config.h>

#include "table-layout.hh"

#include "logging.h"
#include "c++wrap.hh"
#include "setting.hh"

namespace conky {

	template<>
	lua_traits<table_layout::alignment>::Map lua_traits<table_layout::alignment>::map = {
		{ "left",   table_layout::alignment::LEFT },
		{ "center", table_layout::alignment::CENTER },
		{ "right",  table_layout::alignment::RIGHT },
		{ "l",      table_layout::alignment::LEFT },
		{ "c",      table_layout::alignment::CENTER },
		{ "r",      table_layout::alignment::RIGHT }
	};

	table_layout::column table_layout::default_column = {0, alignment::LEFT};

	table_layout::column table_layout::read_column(lua::state &l, size_t colno)
	{
		l.checkstack(1);
		lua::stack_sentry s(l, -1);

		column t = default_column;

		if(not l.istable(-1)) {
			NORM_ERR("table_layout: Column %zd invalid, using defaults...", colno);
			return t;
		}

		l.rawgeti(-1, 1); {
			if(not l.isnumber(-1) || l.tointeger(-1) < 0)
				NORM_ERR("table_layout: Width of column %zd invalid, using defaults...", colno);
			else
				t.width = l.tointeger(-1);
		} l.pop();

		l.rawgeti(-1, 2); {
			try {
				if(not l.isstring(-1))
					NORM_ERR("table_layout: Alignment of column %zd invalid, using defaults...", colno);
				else
					t.align = lua_traits<table_layout::alignment>::from_lua(l, -1,
							strprintf("table_layout alignment of column %zd", colno));
			}
			catch(conky::conversion_error &e) {
				NORM_ERR("%s", e.what());
			}
		} l.pop();

		return t;
	}

	size_t table_layout::read_columns()
	{
		l.checkstack(1);
		lua::stack_sentry s(l, -1);

		if(l.isnil(-1)) {
			NORM_ERR("table_layout: Column specification not present, autodetecting...");
			return 0;
		}

		if(not l.istable(-1)) {
			NORM_ERR("table_layout: Column specification invalid, autodetecting...");
			return 0;
		}
		
		for(size_t i = 1; l.rawgeti(-1, i), !l.isnil(-1); ++i) {
			columns.push_back(read_column(l, i));
		} l.pop();

		return columns.size();
	}

	std::shared_ptr<layout_item> table_layout::read_cell(lua::state &l, size_t rowno, size_t colno)
	{
		lua::stack_sentry s(l, -1);

		try {
			return layout_item::create(l);
		}
		catch(std::runtime_error &e) {
			NORM_ERR("table_layout: Cell (%zd, %zd) invalid.\n%s", rowno, colno, e.what());
			return {};
		}
	}

	table_layout::ItemRow table_layout::read_row(lua::state &l, size_t rowno, size_t cols)
	{
		l.checkstack(1);
		lua::stack_sentry s(l, -1);

		if(not l.istable(-1)) {
			NORM_ERR("table_layout: Skipping invalid row %zd.", rowno);
			return {};
		}

		ItemRow row;

		for(size_t i = 1; l.rawgeti(-1, i), (cols==0?!l.isnil(-1):i<=cols); ++i) {
			row.push_back(read_cell(l, rowno, i));
		} l.pop();

		return row;
	}

	table_layout::table_layout(lua::state &l_)
		: l(l_)
	{
		l.checkstack(1);
		lua::stack_sentry s(l, -1);

		l.rawgetfield(-1, "cols");
		size_t cols = read_columns();

		for(size_t i = 1; l.rawgeti(-1, i), !l.isnil(-1); ++i) {
			item_grid.push_back(read_row(l, i, cols));
		} l.pop();

		scope_ref = l.ref(lua::REGISTRYINDEX);

		if(cols == 0) {
			for(auto i = item_grid.begin(); i != item_grid.end(); ++i) {
				if(i->size() > cols)
					cols = i->size();
			}
			columns.resize(cols, default_column);

			for(auto i = item_grid.begin(); i != item_grid.end(); ++i)
				i->resize(cols, {});
		}
	}

	point::type table_layout::align(point::type have, point::type need, alignment a)
	{
		if(need > have)
			return 0;
		switch(a) {
			case alignment::LEFT:
				return 0;
			case alignment::RIGHT:
				return have-need;
			case alignment::CENTER:
				return (have-need)/2;
			default:
				assert(0);
		}
	}

	table_layout::DataMap::iterator table_layout::make_data(output_method &om)
	{
		auto scope = synchronized(l, [&]() -> std::unique_ptr<const output_method::scope> {
				l.rawgeti(lua::REGISTRYINDEX, scope_ref);
				return om.parse_scope(l);
		});
		return synchronized(data_mutex, [&] {
				return data_map.insert(DataMap::value_type(&om,
						data(std::move(scope), item_grid.size(), item_grid[0].size()))).first;
		});
	}

	point table_layout::size(output_method &om)
	{
		if(empty())
			return { 0, 0 };

		DataMap::iterator data;
		bool end;
		{
			std::lock_guard<std::mutex> lock(data_mutex);
			data = data_map.find(&om);
			end = data == data_map.end();
		}
		if(end)
			data = make_data(om);

		auto old = om.enter_scope(data->second.scope);

		std::vector<point::type> y_data(item_grid.size(), 0);
		std::vector<point::type> x_data(item_grid[0].size(), 0);

		point separator = om.get_max_extents();
		separator.y /= 2;
		point res;
		point::type ypos = 0;
		for(size_t i = 0; i < item_grid.size(); ++i) {
			point::type xpos = 0;
			for(size_t j = 0; j < item_grid[i].size(); ++j) {
				item_data &d = data->second.grid[i][j];
				d.size = item_grid[i][j]->size(om);
				xpos += d.size.x + separator.x;

				x_data[j] = std::max(x_data[j], d.size.x);
				y_data[i] = std::max(y_data[i], d.size.y);
			}
			res.x = std::max(res.x, xpos-separator.x);
			ypos += y_data[i] + separator.y;
		}
		res.y = ypos-separator.y;

		for(size_t j = 0; j < x_data.size(); ++j) {
			if(columns[j].width)
				x_data[j] = columns[j].width;
		}

		ypos = 0;
		for(size_t i = 0; i < item_grid.size(); ++i) {
			point::type xpos = 0;
			for(size_t j = 0; j < item_grid[i].size(); ++j) {
				item_data &d = data->second.grid[i][j];
				d.pos.x = xpos + align(x_data[j], d.size.x, columns[j].align);
				d.pos.y = ypos + align(y_data[i], d.size.y, alignment::CENTER);
				xpos += x_data[j] + separator.x;
			}
			ypos += y_data[i] + separator.y;
		}

		om.leave_scope(std::move(old));

		return res;
	}

	void table_layout::draw(output_method &om, const point &p, const point &size)
	{
		if(empty())
			return;

		DataMap::iterator data;
		{
			std::lock_guard<std::mutex> l(data_mutex);
			data = data_map.find(&om);
			assert(data != data_map.end());
		}

		auto old = om.enter_scope(data->second.scope);

		for(size_t i = 0; i < item_grid.size(); ++i) {
			for(size_t j = 0; j < item_grid[i].size(); ++j) {
				item_data &d = data->second.grid[i][j];

				item_grid[i][j]->draw(om, p+d.pos, min(d.size, size-d.pos));
			}
		}

		om.leave_scope(std::move(old));
	}
}
