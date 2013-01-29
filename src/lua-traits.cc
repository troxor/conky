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

#include "lua-traits.hh"

namespace conky {
	void
	type_check(lua::state &l, int index, lua::Type type1, lua::Type type2,
			const std::string &description)
	{
		lua::Type t = l.type(index);
		if(t != type1 && t != type2) {
			throw conversion_error(std::string("Invalid value of type '") + l.type_name(t)
					+ "' for " + description + ". Expected value of type '"
					+ l.type_name(type1) + "'.");
		}
	}

	point lua_traits<point>::from_lua(lua::state &l, int index, const std::string &description)
	{
		type_check(l, index, lua::TTABLE, lua::TTABLE, description);

		lua::stack_sentry s(l);
		l.checkstack(1);

		point ret;
		l.rawgeti(index, 1); {
			ret.x = lua_traits<point::type>::from_lua(l, -1, "x coordinate of " + description);
		} l.pop();

		l.rawgeti(index, 2); {
			ret.y = lua_traits<point::type>::from_lua(l, -1, "y coordinate of " + description);
		} l.pop();

		return ret;
	}

	void lua_traits<point>::to_lua(lua::state &l, const point &t, const std::string &)
	{
		l.checkstack(2);

		l.createtable(2);

		l.pushinteger(t.x);
		l.rawseti(-2, 1);

		l.pushinteger(t.y);
		l.rawseti(-2, 2);
	}
} /* namespace conky */
