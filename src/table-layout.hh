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

#ifndef TABLE_LAYOUT_HH
#define TABLE_LAYOUT_HH

#include <map>
#include <vector>

#include "list_map.hh"
#include "layout-engine.hh"
#include "luamm.hh"

namespace conky {

	namespace priv {
		struct item {
			point pos;
			point size;
		};

		typedef std::vector<item> row;
		typedef std::vector<row> grid;
	}

	class table_layout: public layout_engine<priv::grid> {
		typedef layout_engine<priv::grid> Base;

	public:
		enum class alignment { LEFT, CENTER, RIGHT };

	private:
		struct column {
			uint32_t width;
			alignment align;
		};
		typedef std::vector<std::shared_ptr<layout_item>> ItemRow;

		std::vector<column> columns;
		std::vector<ItemRow> item_grid;

		static column default_column;

		size_t read_columns(lua::state &l);
		static column read_column(lua::state &l, size_t colno);
		static point::type align(point::type have, point::type need, alignment a);
		ItemRow read_row(lua::state &l, size_t rowno, size_t cols);
		std::shared_ptr<layout_item> read_cell(lua::state &l, size_t rowno, size_t colno);

		bool empty() const { return item_grid.empty() || item_grid[0].empty(); }

	protected:
		virtual priv::grid make_data()
		{ return priv::grid(item_grid.size(), priv::row(item_grid[0].size())); }

		virtual point size(output_method &om, priv::grid &data);
		virtual void draw(output_method &om, const point &p, const point &size, priv::grid &data);

	public:
		table_layout(lua::state &l);
	};
}

#endif /* TABLE_LAYOUT_HH */
