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

#include "data-source.hh"

#include <iostream>
#include <sstream>
#include <unordered_map>

namespace conky {
	namespace {
		/*
		 * Returned when there is no data available.
		 * An alternative would be to throw an exception, but if we don't want to react too
		 * aggresively when the user e.g. uses a nonexisting variable, then returning NaN will do
		 * just fine.
		 */
		float NaN = std::numeric_limits<float>::quiet_NaN();

		typedef std::unordered_map<std::string, lua::cpp_function> data_sources_t;

		/*
		 * We cannot construct this object statically, because order of object construction in
		 * different modules is not defined, so register_source could be called before this
		 * object is constructed. Therefore, we create it on the first call to register_source.
		 */
		data_sources_t *data_sources;

		int data_source_asnumber(lua::state &l)
		{
			l.checkargno(1);
			double x = get_data_source(l, -1)->get_number();
			l.pushnumber(x);
			return 1;
		}

		int data_source_astext(lua::state &l)
		{
			l.checkargno(1);
			std::string x = get_data_source(l, -1)->get_text();
			l.pushstring(x);
			return 1;
		}

		const char data_source__index[] = 
			"local table, key = ...;\n"
			"if key == 'num' then\n"
			"  return conky.asnumber(table);\n"
			"elseif key == 'text' then\n"
			"  return conky.astext(table);\n"
			"else\n"
			"  print(string.format([[Invalid data source operation: '%s']], key));\n"
			"  return 0/0;\n"
			"end\n";

		int factory_wrapper(lua::state &l, const data_source_factory &factory)
		{
			l.createuserdata<std::shared_ptr<data_source_base>>(factory(l));

			l.rawgetfield(lua::REGISTRYINDEX, priv::data_source_metatable);
			l.setmetatable(-2);
			return 1;
		}
	}

	namespace priv {
		void do_register_data_source(const std::string &name, const data_source_factory &factory)
		{
			struct data_source_constructor {
				data_source_constructor()  { data_sources = new data_sources_t(); }
				~data_source_constructor() { delete data_sources; data_sources = NULL; }
			};
			static data_source_constructor constructor;

			bool inserted = data_sources->insert({name,
					std::bind(factory_wrapper, std::placeholders::_1, factory)}).second;
			if(not inserted)
				throw std::logic_error("Data source with name '" + name + "' already registered");
		}
	}

	std::shared_ptr<data_source_base>
	register_disabled_data_source::factory(lua::state &l, const std::string &name, const std::string &setting)
	{
		// XXX some generic way of reporting errors? NORM_ERR?
		std::cerr << "Support for variable '" << name
			<< "' has been disabled during compilation. Please recompile with '"
			<< setting << "'" << std::endl;
		return std::make_shared<simple_numeric_source<float>>(l, std::cref(NaN));
	}

	double data_source_base::get_number() const
	{ return NaN; }

	std::string data_source_base::get_text() const
	{
		std::ostringstream s;
		s << get_number();
		return s.str();
	}

	// at least one data source should always be registered, so data_sources will not be null
	void export_data_sources(lua::state &l)
	{
		lua::stack_sentry s(l);
		l.checkstack(2);

		l.newmetatable(priv::data_source_metatable); {
			l.pushboolean(false);
			l.rawsetfield(-2, "__metatable");

			l.pushdestructor<data_source_base>();
			l.rawsetfield(-2, "__gc");

			l.loadstring(data_source__index);
			l.rawsetfield(-2, "__index");
		} l.pop();

		l.newtable(); {
			for(auto i = data_sources->begin(); i != data_sources->end(); ++i) {
				l.pushfunction(i->second);
				l.rawsetfield(-2, i->first.c_str());
			}
		} l.rawsetfield(-2, "variables");

		l.pushfunction(data_source_asnumber);
		l.rawsetfield(-2, "asnumber");

		l.pushfunction(data_source_astext);
		l.rawsetfield(-2, "astext");
	}
}

/////////// example data sources, remove after real data sources are available ///////
conky::register_disabled_data_source zxcv("zxcv", "BUILD_ZXCV");
