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

#ifndef UTIL_HH
#define UTIL_HH

#include <algorithm>
#include <cstdint>
#include <mutex>

namespace conky {

	struct point {
		typedef int32_t type;

		type x;
		type y;

		point()
			: x(0), y(0)
		{}

		point(type x_, type y_)
			: x(x_), y(y_)
		{}
	};

	inline point equal_point(point::type xy)
	{ return { xy, xy }; }

	inline point operator+(const point &l, const point &r)
	{ return { l.x+r.x, l.y+r.y }; }

	inline point operator-(const point &l, const point &r)
	{ return { l.x-r.x, l.y-r.y }; }

	inline const point& operator+=(point &l, const point &r)
	{ l.x += r.x; l.y += r.y; return l; }

	inline point min(const point &l, const point &r)
	{ return { std::min(l.x, r.x), std::min(l.y, r.y) }; }

	inline point max(const point &l, const point &r)
	{ return { std::max(l.x, r.x), std::max(l.y, r.y) }; }

	inline point operator/(const point &l, int32_t r)
	{ return { l.x/r, l.y/r }; }

	inline bool operator==(const point &l, const point &r)
	{ return l.x == r.x && l.y == r.y; }

	inline bool operator!=(const point &l, const point &r)
	{ return not (l == r); }

	class non_copyable {
		non_copyable(const non_copyable &) = delete;
		const non_copyable& operator=(const non_copyable &) = delete;

	public:
		non_copyable() { }
	};

	template<typename Mutex, typename Fn>
	auto synchronized(Mutex &mutex, const Fn &fn) -> decltype(fn())
	{
		std::lock_guard<Mutex> lock(mutex);
		return std::move(fn());
	}

	template<typename Signed1, typename Signed2>
	bool between(Signed1 value, Signed2 min,
			typename std::enable_if<std::is_signed<Signed1>::value
								== std::is_signed<Signed2>::value, Signed2>::type max)
	{ return value >= min && value <= max; }

	template<typename Signed1, typename Unsigned2>
	bool between(Signed1 value, Unsigned2 min,
			typename std::enable_if<std::is_unsigned<Unsigned2>::value
								&& std::is_signed<Signed1>::value, Unsigned2>::type max)
	{
		return value >= 0
			&& static_cast<typename std::make_unsigned<Signed1>::type>(value) >= min
			&& static_cast<typename std::make_unsigned<Signed1>::type>(value) <= max;
	}

	template<typename Unsigned1, typename Signed2>
	bool between(Unsigned1 value, Signed2 min,
			typename std::enable_if<std::is_signed<Signed2>::value
								&& std::is_unsigned<Unsigned1>::value, Signed2>::type max)
	{
		return max >= 0
			&& value <= static_cast<typename std::make_unsigned<Signed2>::type>(max)
			&& ( min <= 0
				|| value >= static_cast<typename std::make_unsigned<Signed2>::type>(min) );
	}

	std::string format_seconds(std::chrono::seconds seconds);
} /* namespace conky */

#endif /* UTIL_HH */
