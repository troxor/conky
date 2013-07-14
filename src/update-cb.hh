/* -*- mode: c++; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: t -*-
 * vim: ts=4 sw=4 noet ai cindent syntax=cpp
 *
 * Conky, a system monitor, based on torsmo
 *
 * Please see COPYING for details
 *
 * Copyright (C) 2010-2012 Pavel Labath et al.
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

#ifndef UPDATE_CB_HH
#define UPDATE_CB_HH

#include <cstdint>
#include <memory>
#include <thread>
// the following probably requires a is-gcc-4.7.0 check
#include <mutex>
#include <tuple>
#include <unordered_set>

#include <assert.h>

#include "c++wrap.hh"
#include "semaphore.hh"
#include "thread.hh"

namespace conky {
	/*
	 * To create a callback, inherit from this class. The Result template parameter should be the
	 * type of your output, so that your users can retrieve it with the get_result* functions.
	 *
	 * get_result() returns a reference to the internal variable. It can be used without locking
	 * if the object has wait set to true (wait=true means that the run_all_callbacks() waits for
	 * the callback to finish work()ing before returning). If object has wait=false then the user
	 * must first lock the result_mutex.
	 *
	 * get_result_copy() returns a copy of the result object and it handles the necessary
	 * locking. Don't call it if you hold a lock on the result_mutex.
	 *
	 * You should implement the work() function to do the actual updating and store the result in
	 * the result variable (lock the mutex while you are doing it, especially if you have
	 * wait=false).
	 *
	 * The Keys... template parameters are parameters for your work function. E.g., a curl
	 * callback can have one parameter - the url to retrieve,. hddtemp may have two - host and
	 * port number of the hddtemp server, etc. The callbacks.register_thread() function makes
	 * sure that there exists only one object (of the same type) with the same values for all the
	 * keys.
	 *
	 * Callbacks are registered with the callbacks.register_thread() function. You pass the class
	 * name as the template parameter, and any additional parameters to the constructor as
	 * function parameters. The period parameter specifies how often the callback will run. It
	 * should be left for the user to decide that.
	 */
	template<typename Result>
	class result_callback {
	protected:
		Result result;

	public:
		std::mutex result_mutex;

		const Result& get_result()
		{ return result; }

		Result get_result_copy()
		{
			std::lock_guard<std::mutex> l(result_mutex);
			return result;
		}
	};

	template<typename Type, typename Result, typename... Keys>
	class callback: public key_mergable<Type, Keys...>, public result_callback<Result> {
		static_assert(std::is_base_of<task_base, Type>::value,
				"Type must be derived from task_base");

		typedef key_mergable<Type, Keys...> Base;

	public:
		callback(const Keys &... keys) : Base(keys...) { }
		callback(const typename Base::Tuple &tuple) : Base(tuple) { }
	};

	extern task_container<> callbacks;
}

#endif /* UPDATE_CB_HH */
