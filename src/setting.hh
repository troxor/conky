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

#ifndef SETTING_HH
#define SETTING_HH

#include "lua-traits.hh"

namespace conky {

	/*
	 * Checks settings, and does initial calls to the setters.
	 * Should be called after reading the user config.
	 * stack on entry: | ... |
	 * stack on exit:  | ... |
	 */
	void set_config_settings(lua::state &l);

	/*
	 * Calls cleanup functions.
	 * Should be called before exit/restart.
	 * stack on entry: | ... |
	 * stack on exit:  | ... |
	 */
	void cleanup_config_settings(lua::state &l);

	namespace priv {
		class config_setting_base {
		private:
			static void process_setting(lua::state &l, bool init);
			static int config__index(lua::state &l);
			static int config__newindex(lua::state &l);
			static void make_conky_config(lua::state &l);

			// copying is a REALLY bad idea
			config_setting_base(const config_setting_base &) = delete;
			config_setting_base& operator=(const config_setting_base &) = delete;

		protected:
			/*
			 * Set the setting, if the value is sane
			 * stack on entry: | ... new_value |
			 * stack on exit:  | ... |
			 * if the new value doesn't make sense, this function can ignore/alter it
			 */
			virtual void lua_setter(lua::state &l, bool init) = 0;

			/*
			 * Push the current value of the setting to the stack
			 * stack on entry: | ... |
			 * stack on exit:  | ... new_value |
			 */
			virtual void lua_getter(lua::state &l) = 0;

			/*
			 * Called on exit/restart.
			 * stack on entry: | ... |
			 * stack on exit:  | ... |
			 */
			virtual void cleanup() { }

		public:
			const std::string name;
			const size_t seq_no;

			static bool seq_compare(const config_setting_base *a, const config_setting_base *b)
			{ return a->seq_no < b->seq_no; }

			explicit config_setting_base(const std::string &name_);
			virtual ~config_setting_base() {}

			/*
			 * Set the setting manually.
			 * stack on entry: | ... new_value |
			 * stack on exit:  | ... |
			 */
			void lua_set(lua::state &l);

			friend void conky::set_config_settings(lua::state &l);
			friend void conky::cleanup_config_settings(lua::state &l);
		};
	}

	// If you need some very exotic setting, derive it from this class. Otherwise, scroll down.
	template<typename T>
	class config_setting_template: public priv::config_setting_base {
	private:
		class accessor {
		private:
			config_setting_template &setting;

			accessor& operator=(const accessor &) = delete;

		public:
			accessor(config_setting_template &setting_)
				: setting(setting_)
			{}

			accessor(const accessor &r) : setting(r.setting) {}

			operator const T&() const { return setting.get(); }
			const T operator=(const T &r) { return setting.set(r); }

			friend class config_setting_template;
		};
	public:
		explicit config_setting_template(const std::string &name_)
			: config_setting_base(name_)
		{}

		// get the value of the setting
		const T& get() { return value; }
		// set the value of the setting
		virtual const T set(const T &r, bool /*init*/ = false) { return value = r; }
		virtual const T set_default(bool init = false) = 0;

		accessor operator*() { return accessor(*this); }
		const T& operator*() const { return value; }

		T* operator->() { return &value; }
		const T* operator->() const { return &value; }

	protected:
		T value;
	};

	/*
	 * Declares a setting <name> in the conky.config table.
	 * Getter function is used to translate the lua value into C++. It recieves the value on the
	 * lua stack. It should pop it and return the C++ value. In case the value is nil, it should
	 * return a predefined default value. Translation into basic types works with the help of
	 * lua_traits template above
	 * The lua_setter function is called when someone tries to set the value.  It recieves the
	 * new and the old value on the stack (old one is on top). It should return the new value for
	 * the setting. It doesn't have to be the value the user set, if e.g. the value doesn't make
	 * sense. The second parameter is true if the assignment occurs during the initial parsing of
	 * the config file, and false afterwards. Some settings obviously cannot be changed (easily?)
	 * when conky is running, but some (e.g. x/y position of the window) can.
	 */
	template<typename T, typename Traits = lua_traits<T>>
	class simple_config_setting: public config_setting_template<T>, private Traits {
		typedef config_setting_template<T> Base;

	public:
		simple_config_setting(const std::string &name_, const T &default_value_ = T(),
												bool modifiable_ = false, Traits traits = Traits())
			: Base(name_), Traits(traits), default_value(default_value_), modifiable(modifiable_)
		{}

		virtual const T set_default(bool init = false)
		{ return this->set(default_value, init); }

	protected:
		const T default_value;
		const bool modifiable;

		virtual std::pair<T, bool> do_convert(lua::state &l, int index);
		virtual void lua_setter(lua::state &l, bool init);
		virtual void lua_getter(lua::state &l)
		{ Traits::to_lua(l, Base::get(), "setting '" + Base::name + '\''); }
	};

	template<typename T, typename Traits>
	std::pair<T, bool>
	simple_config_setting<T, Traits>::do_convert(lua::state &l, int index)
	{
		try {
			return { Traits::from_lua(l, index, "setting '" + Base::name + '\''), true };
		}
		catch(const conversion_error &e) {
			NORM_ERR("%s", e.what());
			return {default_value, false};
		}
	}

	template<typename T, typename Traits>
	void simple_config_setting<T, Traits>::lua_setter(lua::state &l, bool init)
	{
		lua::stack_sentry s(l, -1);

		if(!init && !modifiable)
			NORM_ERR("Setting '%s' is not modifiable", Base::name.c_str());
		else {
			if(l.isnil(-1))
				set_default(init);
			else {
				auto r = do_convert(l, -1);
				if(r.second)
					this->set(r.first, init);
				else
					set_default(init);
			}
		}
	}

	// Just like simple_config_setting, except that in only accepts value in the [min, max] range
	// This class is here only for convenience and backward compatibility. It does not offer any
	// additional functionality.
	template<typename T, typename Traits_ = lua_traits<T>>
	class range_config_setting: public simple_config_setting<T, range_traits<T, Traits_>> {
		typedef simple_config_setting<T, range_traits<T, Traits_>> Base;

	public:
		range_config_setting(const std::string &name_,
						const T &min = std::numeric_limits<T>::min(),
						const T &max = std::numeric_limits<T>::max(),
						const T &default_value_ = T(),
						bool modifiable_ = false)
			: Base(name_, default_value_, modifiable_, range_traits<T, Traits_>(min, max))
		{ assert(min <= Base::default_value); assert(Base::default_value <= max); }
	};
}

#endif /* SETTING_HH */
