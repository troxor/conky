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

#include <vector>

#include "layout-item.hh"
#include "luamm.hh"

namespace conky {

	class table_layout: public layout_item {
	public:
		enum class alignment { LEFT, CENTER, RIGHT };

	private:
		struct column {
			uint32_t width;
			alignment align;
		};
		typedef std::vector<std::shared_ptr<layout_item>> Row;

		std::vector<column> columns;
		std::vector<Row> grid;

		static column default_column;

		size_t read_columns(lua::state &l);
		column read_column(lua::state &l, size_t colno);

		Row read_row(lua::state &l, size_t rowno, size_t cols);
		std::shared_ptr<layout_item> read_cell(lua::state &l, size_t rowno, size_t colno);

	public:
		table_layout(lua::state &l);

		virtual point size(const output_method &om);

		virtual void draw(output_method &om, const point &p, const point &dim);
	};
}

#endif /* TABLE_LAYOUT_HH */
