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
					t.align = conky::from_lua<table_layout::alignment>(l, -1, 
							strprintf("table_layout alignment of column %zd", colno));
			}
			catch(conky::conversion_error &e) {
				NORM_ERR("%s", e.what());
			}
		} l.pop();

		return t;
	}

	size_t table_layout::read_columns(lua::state &l)
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

	table_layout::table_layout(lua::state &l)
	{
		l.checkstack(1);
		lua::stack_sentry s(l, -1);

		l.rawgetfield(-1, "cols");
		size_t cols = read_columns(l);

		for(size_t i = 1; l.rawgeti(-1, i), !l.isnil(-1); ++i) {
			item_grid.push_back(read_row(l, i, cols));
		} l.pop();

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

	point table_layout::size(const output_method &om)
	{
		if(item_grid.empty())
			return { 0, 0 };

		DataMap::iterator data;
		{
			std::lock_guard<std::mutex> l(data_mutex);
			data = data_map.find(&om);
			if(data == data_map.end()) {
				data = data_map.insert(DataMap::value_type(&om,
								DataGrid(item_grid.size(), DataRow(item_grid[0].size())))).first;
			}
		}

		point res;
		int32_t ypos = 0;
		for(size_t i = 0; i < item_grid.size(); ++i) {
			int32_t height = 0;
			int32_t xpos = 0;
			for(size_t j = 0; j < item_grid[i].size(); ++j) {
				item_data &d = data->second[i][j];
				d.size = item_grid[i][j]->size(om);
				d.pos = { xpos, ypos };
				height = std::max(height, d.size.y);
				xpos += d.size.x + 5;
			}
			ypos += height + 5;
		}
		return res;
	}

	void table_layout::draw(output_method &om, const point &p, const point &size)
	{
		if(item_grid.empty())
			return;

		DataMap::iterator data;
		{
			std::lock_guard<std::mutex> l(data_mutex);
			data = data_map.find(&om);
			assert(data != data_map.end());
		}

		for(size_t i = 0; i < item_grid.size(); ++i) {
			for(size_t j = 0; j < item_grid[i].size(); ++j) {
				item_data &d = data->second[i][j];

				item_grid[i][j]->draw(om, p+d.pos, min(d.size, size-d.pos));
			}
		}

	}
}
