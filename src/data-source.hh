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

#ifndef DATA_SOURCE_HH
#define DATA_SOURCE_HH

#include <limits>
#include <string>
#include <type_traits>

#include "luamm.hh"
#include "layout-item.hh"

namespace conky {

	/*
	 * A base class for all data sources.
	 * API consists of two functions:
	 * - get_number should return numeric representation of the data (if available). This can
	 *   then be used when drawing graphs, bars, ... The default implementation returns NaN.
	 * - get_text should return textual representation of the data. This is used when simple
	 *   displaying the value of the data source. The default implementation converts
	 *   get_number() to a string, but you can override to return anything (e.g. add units)
	 */
    class data_source_base: public layout_item {
    public:
		virtual point size(output_method &om)
		{ return om.get_text_size(get_text()); }

		virtual void draw(output_method &om, const point &p, const point &dim)
		{ om.draw_text(get_text(), p, dim); }


        virtual double get_number() const;
        virtual std::string get_text() const;

		template<typename T, typename... Args>
		static std::shared_ptr<T> make(lua::state &l, Args &&... args)
		{ return std::shared_ptr<T>(new T(l, std::forward<Args>(args)...)); }
    };

	typedef std::function<std::shared_ptr<data_source_base> (lua::state &)> data_source_factory;

	/// A simple data source returning some fixed string.
	class string_source: public data_source_base {
	public:
		explicit string_source(lua::state &l, const std::string &text_)
			: text(text_)
		{ l.pop(); }

        virtual std::string get_text() const { return text; }

	private:
		std::string text;
	};

	/**
	 * A simple data source that returns the value of some variable. It ignores the lua table.
	 * The source variable can be specified as a fixed parameter to the register_data_source
	 * constructor, or one can create a subclass and then set the source from the subclass
	 * constructor.
	 * XXX: do we need this?
	 */
	template<typename T>
	class simple_numeric_source: public data_source_base {
		static_assert(std::is_convertible<T, double>::value, "T must be convertible to double");

		const T &source;
	public:
		simple_numeric_source(lua::state &l, const T &source_)
			: source(source_)
		{ l.pop(); }

		virtual double get_number() const
		{ return source; }
	};

	/*
	 * This is a part of the implementation, but it cannot be in .cc file because the template
	 * functions below call it
	 */
	namespace priv {
		const char data_source_metatable[] = "conky::data_source_metatable";

		void do_register_data_source(const std::string &name, const data_source_factory &factory);

	}

	/*
	 * Declaring an object of this type at global scope will register a data source with the
	 * given name.
	 */
	class register_data_source {
/*		template<typename... Args>
		static int factory(lua::state *l, const std::string &name, const Args&... args)
		{
		}*/

	public:
		register_data_source(const std::string &name, const data_source_factory &factory)
		{ priv::do_register_data_source(name, factory); }

		template<typename Functor, typename... Args>
		register_data_source(const std::string &name, Functor functor,
				Args&&... args)
		{
			priv::do_register_data_source( name, std::bind(functor, std::placeholders::_1,
						std::forward<Args>(args)...
				)); 
		}
	};

	/*
	 * Use this to declare a data source that has been disabled during compilation. We can then
	 * print a nice error message telling the used which setting to enable.
	 */
	class register_disabled_data_source: public register_data_source {
		static
		std::shared_ptr<data_source_base>
		factory(lua::state &l, const std::string &name, const std::string &setting);

	public:
		register_disabled_data_source(const std::string &name, const std::string &setting)
			: register_data_source(name, &factory, name, setting)
		{}
	};

	/*
	 * It expects to have a table at the top of lua stack. It then exports all the data sources
	 * into that table (actually, into a "variables" subtable).
	 */
	void export_data_sources(lua::state &l);

	inline const std::shared_ptr<data_source_base> &get_data_source(lua::state &l, int index)
	{ return *l.checkudata<std::shared_ptr<data_source_base>>(index, priv::data_source_metatable); }

	template<typename T>
	std::shared_ptr<T> get_data_source(lua::state &l, int index)
	{
		auto t = std::dynamic_pointer_cast<T>(get_data_source(l, index));
		if(not t)
			throw std::runtime_error("Data source is not of requested type.");
		return t;
	}
}

#endif /* DATA_SOURCE_HH */
