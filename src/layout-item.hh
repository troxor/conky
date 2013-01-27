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

#ifndef LAYOUT_ITEM_HH
#define LAYOUT_ITEM_HH

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "luamm.hh"
#include "output-method.hh"

namespace conky {

	/**
	 * A base class for everything that can be displayed on the screen.
	 */
	class layout_item {
	public:
		virtual ~layout_item() {}

		/// Calculates the space this item will occupy when drawn to om.
		virtual point size(output_method &om) = 0;

		/**
		 * Draws this item to om, p contains the coordinates of the top-left corner, p+size is
		 * the lower-right corner.
		 */
		virtual void draw(output_method &om, const point &p, const point &size) = 0;

		static std::shared_ptr<layout_item> create(lua::state &l);
	};

	/*
	 * It expects to have a table at the top of lua stack. It then exports all the layout engines
	 * into that table.
	 */
	void export_layout_engines(lua::state &l);

}

#endif /* LAYOUT_ITEM_HH */
