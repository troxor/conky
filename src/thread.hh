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

#ifndef THREAD_HH
#define THREAD_HH

#include <cstdint>
#include <memory>
#include <thread>
#include <mutex>
#include <tuple>
#include <unordered_set>

#include <assert.h>

#include "c++wrap.hh"
#include "semaphore.hh"

namespace conky {
	// forward declarations
	template<typename Thread>
	class thread_handle;

	/*
	 * Base class for threads that can be stored in a thread container, which can then be used
	 * for managing them collectively. A descendant class should implement:
	 * - work(): the actual work to be done. It is run whenever someone calls run_all_threads on
	 *   the thread_container
	 * - operator==(): optional. If you want the container to automatically merge multiply
	 *   registered threads. This operator determines if two threads are considered identical.
	 * - merge(): optional, used when merging two threads. Use it to transfer any interesting
	 *   info from the other thread, before it is destroyed.
	 */
	class thread_base {
		semaphore sem_start;
		std::thread *thread;
		const size_t hash;
		uint32_t period;
		uint32_t remaining;
		std::pair<int, int> pipefd;
		const bool wait;
		bool done;
		uint8_t unused;

		thread_base(const thread_base &) = delete;
		thread_base& operator=(const thread_base &) = delete;

		virtual bool operator==(const thread_base &) { return false; }

		void run(semaphore &sem_wait);
		void start_routine(semaphore &sem_wait);
		void stop();

		static void deleter(thread_base *ptr)
		{
			ptr->stop();
			delete ptr;
		}

		friend class thread_container;

	protected:
		/*
		 * Construct a new thread. Parameters:
		 * - hash: the hash value of this thread. Only threads with the same hash are considered
		 *   for merging.
		 * - period: the periodicity of this thread. Every period-th call to
		 *   container.run_all_threads() calls work() in this thread.
		 * - wait: should run_all_threads() wait for work() to complete before returning?
		 * - use_pipe: should the container use a pipe to signal this thread to terminate? One
		 *   can then use select() on the descriptor returned by donefd() to wait for the
		 *   termination signal.
		 */
		thread_base(size_t hash_, uint32_t period_, bool wait_, bool use_pipe)
			: thread(NULL), hash(hash_), period(period_), remaining(0),
			  pipefd(use_pipe ? pipe2(O_CLOEXEC) : std::pair<int, int>(-1, -1)),
			  wait(wait_), done(false), unused(0)
		{}

		int donefd()
		{ return pipefd.first; }

		bool is_done()
		{ return done; }

		virtual void work() = 0;
		virtual void merge(thread_base &&);

	public:
		virtual ~thread_base();
	};

	/*
	 * A class used for manipulating threads stored in a thread_container. It's a restricted
	 * version of shared_ptr.
	 */
	template<typename Thread>
	class thread_handle: private std::shared_ptr<Thread> {
		typedef std::shared_ptr<Thread> Base;
	
		thread_handle(Thread *ptr)
			: Base(ptr, &thread_base::deleter)
		{}

		thread_handle(Base &&ptr)
			: Base(std::move(ptr))
		{}

		friend class thread_container;
	public:

		using Base::operator->;
		using Base::operator*;
	};
		
	/*
	 * Threads are registered with the register_thread() function. You pass the class name as the
	 * template parameter, and any additional parameters to the constructor as function
	 * parameters. The period parameter specifies how often the thread will run.
	 * register_thread() returns a handle to the newly created object. As long as someone holds
	 * the object handle, the thread will be run.
	 */
	class thread_container {
		typedef thread_handle<thread_base> handle;
		typedef std::unordered_set<handle, size_t (*)(const handle &),
								   bool (*)(const handle &, const handle &)>
		Threads;

		semaphore sem_wait;
		Threads threads;

		static size_t get_hash(const handle &h) { return h->hash; }
		static bool is_equal(const handle &a, const handle &b);

		handle do_register_thread(const handle &h);

	public:
		thread_container()
			: threads(1, get_hash, is_equal)
		{}

		/*
		 * Constructs a thread by passing the parameters to its constructor and registers it with
		 * the container.
		 */
		template<typename Thread, typename... Params>
		thread_handle<Thread> register_thread(uint32_t period, Params&&... params)
		{
			static_assert(std::is_base_of<thread_base, Thread>::value,
					"Thread must be a descendant of thread_base");

			return std::dynamic_pointer_cast<Thread>(do_register_thread(
						handle(new Thread(period, std::forward<Params>(params)...))
					));
		}

		void run_all_threads();
	};

	namespace priv {
		template<size_t pos, typename... Elements>
		struct hash_tuple {
			typedef std::tuple<Elements...>                         Tuple;
			typedef typename std::tuple_element<pos-1, Tuple>::type Element;

			static inline size_t hash(const Tuple &tuple)
			{
				return std::hash<Element>()(std::get<pos-1>(tuple))
						+ 47 * hash_tuple<pos-1, Elements...>::hash(tuple);
			}
		};

		template<typename... Elements>
		struct hash_tuple<0, Elements...> {
			static inline size_t hash(const std::tuple<Elements...> &)
			{ return 0; }
		};
	}

	/*
	 * A class for threads, which automatically handles merging of identical objects. Two threads
	 * are considered identical if the have the same value for all Keys template parameters.
	 */
	template<typename... Keys>
	class thread: public thread_base {
		virtual bool operator==(const thread_base &other)
		{ return tuple == dynamic_cast<const thread &>(other).tuple; }

	public:
		typedef std::tuple<Keys...> Tuple;

	protected:
		const Tuple tuple;

		template<size_t i>
		typename std::add_lvalue_reference<
					const typename std::tuple_element<i, Tuple>::type
				>::type
		get()
		{ return std::get<i>(tuple); }

	public:
		thread(uint32_t period_, bool wait_, const Tuple &tuple_, bool use_pipe = false)
			: thread_base(priv::hash_tuple<sizeof...(Keys), Keys...>::hash(tuple_),
						period_, wait_, use_pipe),
			  tuple(tuple_)
		{}
	};
}

#endif /* THREAD_HH */
