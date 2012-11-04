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

#include <limits>
#include <string>
#include <type_traits>

#include "logging.h"
#include "luamm.hh"
#include "thread.hh"

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

	class conversion_error: public std::runtime_error {
	public:
		conversion_error(const std::string &msg)
			: std::runtime_error(msg)
		{}
	};

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

	namespace priv {
		void type_check(lua::state &l, int index, lua::Type type1, lua::Type type2,
				const std::string &description);
	}

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
			priv::type_check(l, index, lua::TNUMBER, lua::TSTRING, description);

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
			priv::type_check(l, index, lua::TNUMBER, lua::TSTRING, description);

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
			priv::type_check(l, index, lua::TSTRING, lua::TSTRING, description);

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
			priv::type_check(l, index, lua::TBOOLEAN, lua::TBOOLEAN, description);

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
			priv::type_check(l, index, lua::TSTRING, lua::TSTRING, description);

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

	namespace priv {
		class config_setting_base {
		private:
			static void process_setting(lua::state &l, bool init);
			static int config__index(lua::state *l);
			static int config__newindex(lua::state *l);
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

			accessor(const accessor &r) : setting(r.setting) {}
			accessor& operator=(const accessor &) = delete;

		public:
			accessor(config_setting_template &setting_)
				: setting(setting_)
			{}

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
	class simple_config_setting: public config_setting_template<T> {
		typedef config_setting_template<T> Base;

	public:
		simple_config_setting(const std::string &name_, const T &default_value_ = T(),
													bool modifiable_ = false)
			: Base(name_), default_value(default_value_), modifiable(modifiable_)
		{}

		virtual const T set_default(bool init = false)
		{ return set(default_value, init); }

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
					set(r.first, init);
			}
		}
	}

	// Just like simple_config_setting, except that in only accepts value in the [min, max] range
	template<typename T, typename Traits = lua_traits<T>>
	class range_config_setting: public simple_config_setting<T, Traits> {
		typedef simple_config_setting<T, Traits> Base;

		const T min;
		const T max;
	public:
		range_config_setting(const std::string &name_,
						const T &min_ = std::numeric_limits<T>::min(),
						const T &max_ = std::numeric_limits<T>::max(),
						const T &default_value_ = T(),
						bool modifiable_ = false)
			: Base(name_, default_value_, modifiable_), min(min_), max(max_)
		{ assert(min <= Base::default_value && Base::default_value <= max); }

		virtual const T set(const T &r, bool init)
		{
			if(!between(r, min, max)) {
				NORM_ERR("Value is out of range for setting '%s'", Base::name.c_str());
				// we ignore out-of-range values. an alternative would be to clamp them. do we
				// want to do that?
				return Base::value;
			}
			return Base::set(r, init);
		}
	};

/////////// example settings, remove after real settings are available ///////
	extern range_config_setting<int> asdf;
}

#endif /* SETTING_HH */
