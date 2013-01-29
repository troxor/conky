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

#ifndef LUA_TRAITS_HH
#define LUA_TRAITS_HH

#include "logging.h"
#include "luamm.hh"
#include "util.hh"

namespace conky {
	class conversion_error: public std::runtime_error {
	public:
		conversion_error(const std::string &msg)
			: std::runtime_error(msg)
		{}
	};

	void type_check(lua::state &l, int index, lua::Type type1, lua::Type type2,
			const std::string &description);

	template<typename T,
		bool is_integral = std::is_integral<T>::value,
		bool floating_point = std::is_floating_point<T>::value,
		bool is_enum = std::is_enum<T>::value>
	struct lua_traits
	{
		// integral is here to force the compiler to evaluate the assert at instantiation time
		static_assert(is_integral && false,
			"Only specializations for enum, string, integral and floating point types are available");
	};

	// specialization for integral types
	template<typename T>
	struct lua_traits<T, true, false, false> {
		static T
		from_lua(lua::state &l, int index, const std::string &description)
		{
			type_check(l, index, lua::TNUMBER, lua::TSTRING, description);

			lua::integer t = l.tointeger(index);
			if(not between(t, std::numeric_limits<T>::min(), std::numeric_limits<T>::max()))
				throw conversion_error(std::string("Value out of range for " + description + '.'));

			return t;
		}

		static void to_lua(lua::state &l, const T &t, const std::string &description)
		{
			if(not between(t, std::numeric_limits<lua::integer>::min(),
						      std::numeric_limits<lua::integer>::max()))
				throw conversion_error(std::string("Value out of range for " + description + '.'));
			l.pushinteger(t);
		}
	};

	// specialization for floating point types
	template<typename T>
	struct lua_traits<T, false, true, false> {
		static inline T
		from_lua(lua::state &l, int index, const std::string &description)
		{
			type_check(l, index, lua::TNUMBER, lua::TSTRING, description);

			return l.tonumber(index);
		}

		static inline void to_lua(lua::state &l, const T &t, const std::string &)
		{ l.pushnumber(t); }
	};

	// specialization for std::string
	template<>
	struct lua_traits<std::string, false, false, false> {
		static inline std::string
		from_lua(lua::state &l, int index, const std::string &description)
		{
			type_check(l, index, lua::TSTRING, lua::TSTRING, description);

			return l.tostring(index);
		}

		static inline void to_lua(lua::state &l, const std::string &t, const std::string &)
		{ l.pushstring(t); }
	};

	// specialization for bool
	template<>
	struct lua_traits<bool, true, false, false> {
		static inline bool
		from_lua(lua::state &l, int index, const std::string &description)
		{
			type_check(l, index, lua::TBOOLEAN, lua::TBOOLEAN, description);

			return l.toboolean(index);
		}

		static inline void to_lua(lua::state &l, bool t, const std::string &)
		{ l.pushboolean(t); }
	};

	// specialization for enums
	// to use this, one first has to declare string<->value map
	template<typename T>
	struct lua_traits<T, false, false, true> {
		typedef std::initializer_list<std::pair<std::string, T>> Map;
		static Map map;

		static T
		from_lua(lua::state &l, int index, const std::string &description)
		{
			type_check(l, index, lua::TSTRING, lua::TSTRING, description);

			const std::string &val = l.tostring(index);

			for(auto i = map.begin(); i != map.end(); ++i) {
				if(i->first == val)
					return i->second;
			}

			std::string msg = "Invalid value '" + val + "' for "
				+ description + ". Valid values are: ";
			for(auto i = map.begin(); i != map.end(); ++i) {
				if(i != map.begin())
					msg += ", ";
				msg += '\'' + i->first + '\'';
			}
			msg += '.';
			throw conversion_error(msg);
		}

		static void to_lua(lua::state &l, const T &t, const std::string &description)
		{
			for(auto i = map.begin(); i != map.end(); ++i) {
				if(i->second == t) {
					l.pushstring(i->first);
					return;
				}
			}
			throw conversion_error("Invalid value for " + description + ".");
		}
	};

	// specialization for points ([x, y] pairs)
	template<>
	struct lua_traits<point> {
		static point from_lua(lua::state &l, int index, const std::string &description);
		static void to_lua(lua::state &l, const point &t, const std::string &);
	};

	template<typename T, typename Traits = lua_traits<T>>
	class range_traits: private Traits {
		const T min;
		const T max;

	public:
		range_traits(const T &min_, const T &max_) : min(min_), max(max_) { assert(min <= max); }

		T from_lua(lua::state &l, int index, const std::string &description);
		using Traits::to_lua;
	};

	template<typename T, typename Traits>
	T range_traits<T, Traits>::from_lua(lua::state &l, int index, const std::string &description)
	{
		T t = Traits::from_lua(l, index, description);
		if(t < min) {
			NORM_ERR("Value too small for %s. Adjusting.", description.c_str());
			t = min;
		}
		if(t > max) {
			NORM_ERR("Value too large for %s. Adjusting.", description.c_str());
			t = max;
		}
		return t;
	}
} /* namespace conky */

#endif /* LUA_TRAITS_HH */
