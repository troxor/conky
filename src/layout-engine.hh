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

#ifndef LAYOUT_ENGINE_HH
#define LAYOUT_ENGINE_HH

#include "layout-item.hh"

namespace conky {

	template<typename Data>
	class layout_engine: public layout_item {
		typedef std::pair<std::unique_ptr<const output_method::scope>, Data> Pair;
		typedef list_map<const output_method *, Pair> DataMap;

		lua::state &l;
		DataMap data_map;
		std::mutex data_mutex;
		int scope_ref;

	protected:
		layout_engine(lua::state &l_)
			: l(l_), scope_ref(l.ref(lua::REGISTRYINDEX))
		{ l.rawgeti(lua::REGISTRYINDEX, scope_ref); }

		virtual Data make_data() = 0;
		virtual point size(output_method &om, Data &data) = 0;
		virtual void draw(output_method &om, const point &p, const point &size, Data &data) = 0;

	public:
		virtual ~layout_engine() { l.unref(lua::REGISTRYINDEX, scope_ref); }

		virtual point size(output_method &om);
		virtual void draw(output_method &om, const point &p, const point &size)
		{
			auto data = synchronized(data_mutex, [&] { return data_map.find(&om); });
			auto old = om.enter_scope(data->second.first);
			draw(om, p, size, data->second.second);
			om.leave_scope(std::move(old));
		}
	};

	template<typename Data>
	point layout_engine<Data>::size(output_method &om)
	{
		typename DataMap::iterator data;
		bool end;
		{
			std::lock_guard<std::mutex> lock(data_mutex);
			data = data_map.find(&om);
			end = data == data_map.end();
		}
		if(end) {
			auto scope = synchronized(l, [&]() -> std::unique_ptr<const output_method::scope> {
					l.rawgeti(lua::REGISTRYINDEX, scope_ref);
					return om.parse_scope(l);
				});
			data = synchronized(data_mutex, [&] {
					return data_map.insert(typename DataMap::value_type(&om,
							Pair(std::move(scope), std::move(this->make_data()) ) )).first;
				});
		}

		auto old = om.enter_scope(data->second.first);

		point res = size(om, data->second.second);

		om.leave_scope(std::move(old));

		return res;
	}
} /* namespace conky */

#endif /* LAYOUT_ENGINE_HH */
