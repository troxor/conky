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

#ifndef OUTPUT_METHOD_HH
#define OUTPUT_METHOD_HH

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "list_map.hh"
#include "luamm.hh"
#include "thread.hh"
#include "util.hh"

namespace conky {

	class output_method {
	public:
		virtual ~output_method() { }

		class scope: private conky::non_copyable {
		public:
			virtual ~scope() { }
		};

		virtual std::unique_ptr<const scope> parse_scope(lua::state &l) = 0;
		virtual std::unique_ptr<const scope> enter_scope(const std::unique_ptr<const scope> &s) = 0;
		virtual void leave_scope(std::unique_ptr<const scope> &&s) = 0;

		virtual point get_max_extents() const = 0;
		virtual point get_text_size(const std::u32string &text) const = 0;
		virtual point get_text_size(const std::string &text) const = 0;
		virtual void draw_text(const std::u32string &text, const point &p, const point &size) = 0;
		virtual void draw_text(const std::string &text, const point &p, const point &size) = 0;
	};

	extern task_container<output_method> output_methods;
}

#endif /* OUTPUT_METHOD_HH */
