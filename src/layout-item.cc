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

#include <config.h>

#include "layout-item.hh"

#include "logging.h"
#include "data-source.hh"
#include "table-layout.hh"

namespace conky {
	namespace {
		const char layout_engine_metatable[] = "conky::layout_engine_metatable";

		template<typename T>
		int layout_factory(lua::state *l)
		{
			static_assert(std::is_base_of<layout_item, T>::value,
							"T must be derived from conky::layout_item");

			l->createuserdata<std::shared_ptr<layout_item>>( std::make_shared<T>(*l) );

			l->rawgetfield(lua::REGISTRYINDEX, layout_engine_metatable);
			l->setmetatable(-2);

			return 1;
		}
	}

	std::shared_ptr<layout_item> layout_item::create(lua::state &l)
	{
		lua::stack_sentry s(l, -1);
		l.checkstack(1);

		switch(l.type(-1)) {
			case lua::TSTRING:
				return std::make_shared<string_source>(l.tostring(-1));

			case lua::TUSERDATA:
			case lua::TLIGHTUSERDATA:
				try {
					return get_data_source(l, -1);
				}
				catch(lua::check_error &) {
				}
				try {
					/// XXX: detect layout items
				}
				catch(...) {
				}
			default:
				NORM_ERR("Unrecognized type of parameter: %s", l.type_name(l.type(-1)));
				return {};
		}
	}

	void export_layout_engines(lua::state &l)
	{
		lua::stack_sentry s(l);
		l.checkstack(2);

		l.newmetatable(layout_engine_metatable); {
			l.pushboolean(false);
			l.rawsetfield(-2, "__metatable");

			l.pushdestructor<std::shared_ptr<layout_item>>();
			l.rawsetfield(-2, "__gc");
		} l.pop();

		l.pushfunction(&layout_factory<table_layout>);
		l.rawsetfield(-2, "table");
	}
}
