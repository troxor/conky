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

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <thread>
#include <mutex>
#include <queue>
#include <tuple>
#include <unordered_set>

#include <assert.h>

#include "c++wrap.hh"
#include "semaphore.hh"
#include "util.hh"

namespace conky {

	/**
	 * Base class for tasks that can be stored in a task container, which can then be used
	 * for managing them collectively. Deriveded class should implement:
	 * - tick(): the actual work to be done. It is run whenever someone calls run_all_tasks on
	 *   the task_container
	 * - operator==(): optional. If you want the container to automatically merge multiply
	 *   registered tasks. This operator determines if two tasks are considered equivalent.
	 * - merge(): optional, used when merging two tasks. Use it to transfer any interesting
	 *   info from the other task, before it is destroyed.
	 * Tasks shold be used for work, which doesn't take long (doesn't block waiting for network
	 * connections or similar).
	 */
	class task_base {
	public:
		virtual ~task_base() { }

		virtual void tick() = 0;
		virtual void merge(task_base &&) { }
		virtual bool operator==(const task_base &) { return false; }
		virtual size_t get_hash() const { return reinterpret_cast<size_t>(this); }

	};

	/**
	 * Base class for tasks that can take an unpredictible amount of time. For this reason, they
	 * are run in a separate dedicated thread. You should do your work in the work(), which will
	 * be the main function of the thread. When the work is done, you should use the wait()
	 * function to wait for the next update signal. If is_done() returns true, you should return
	 * from the work function.
	 */
	class thread_task: public task_base {
		semaphore start;
		bool done;

	protected:
		bool is_done() const { return done; }
		void wait() { start.wait(); }

	public:
		thread_task() : done(false) { }

		virtual void tick() { start.post(); }
		virtual void stop() { done = true; tick(); }
		virtual void work() = 0;
	};

	/**
	 * Just like the previous class, only it additionally signals the update by writing to a
	 * pipe. This enables waiting for the update using select() et al.
	 */
	class piped_thread: public thread_task {
		typedef thread_task Base;

		std::pair<int, int> pipefd;

	protected:
		enum signal { DONE, NEXT };

		int signalfd() { return pipefd.first; }

		// If there is no signal on signalfd(), it will block.
		signal get_signal();

	public:
		virtual void tick();
		virtual void stop();

		piped_thread()
			: pipefd(pipe2(O_CLOEXEC))
		{ fcntl_setfl(pipefd.second, fcntl_getfl(pipefd.second) | O_NONBLOCK); }

		virtual ~piped_thread();
	};

	/*
	 * A class for tasks, which automatically handles merging of identical objects. Two tasks
	 * are considered identical if the have the same value for all Keys template parameters. You
	 * can choose whether to apply this template to task_base, thread_task or piped_thread.
	 */
	template<typename Base, typename... Keys>
	class key_mergable: public Base {
	public:
		typedef std::tuple<Keys...> Tuple;

	protected:
		const Tuple tuple;

		template<size_t i>
		typename std::add_lvalue_reference<const typename std::tuple_element<i, Tuple>::type>::type
		get()
		{ return std::get<i>(tuple); }

		virtual bool operator==(const task_base &other)
		{ return tuple == dynamic_cast<const key_mergable &>(other).tuple; }

		key_mergable(const Keys &... keys) : tuple(keys...) { }
		key_mergable(const Tuple &tuple) : tuple(tuple) { }
	};

	namespace priv {
		class task_holder: private non_copyable {
			enum {UNUSED_MAX = 5};

			uint32_t period;
			uint32_t remaining;
			uint8_t unused;

		protected:

			virtual bool unique() = 0;
			virtual task_base& get_task() const = 0;
			virtual void run() = 0;

		public:
			const size_t hash;

			bool tick();

			bool operator==(const task_holder &other) const
			{ return get_task() == other.get_task(); }

			void merge(task_holder &&other);
			virtual std::shared_ptr<task_base> get_ptr() = 0;

			task_holder(uint32_t period_, size_t hash_)
				: period(period_), remaining(0), unused(0), hash(hash_)
			{ }

			virtual ~task_holder() { }
		};

		class single_task_holder: public task_holder {
			std::shared_ptr<thread_task> task;
			std::thread *thread;

		protected:
			virtual void run();
			virtual bool unique() { return task.unique(); }
			virtual task_base& get_task() const { return *task; }

		public:
			single_task_holder(uint32_t period, const std::shared_ptr<thread_task> &task_)
				: task_holder(period, task_->get_hash()), task(task_), thread(NULL)
			{ }
			virtual ~single_task_holder();

			virtual std::shared_ptr<task_base> get_ptr() { return task; }
		};

		class task_pool {
			std::mutex mutex;
			std::condition_variable cv;
			std::queue<std::shared_ptr<task_base>> queue;
			std::vector<std::thread> threads;
			semaphore sem;
			uint32_t to_wait;
			bool done;

			void runner();

		public:
			task_pool(size_t threads_)
				: to_wait(0), done(false)
			{
				for(size_t i = 0; i < threads_; ++i)
					threads.emplace_back(&task_pool::runner, this);
			}

			~task_pool();

			void add(const std::shared_ptr<task_base> &handle)
			{
				++to_wait;
				std::lock_guard<std::mutex> lock(mutex);
				queue.push(handle);
				cv.notify_one();
			}

			void wait() { while(to_wait > 0) { sem.wait(); --to_wait; } }
		};

		class pooled_task_holder: public task_holder {
			task_pool &pool;
			std::shared_ptr<task_base> task;

		protected:
			virtual void run() { pool.add(task); }
			virtual bool unique() { return task.unique(); }
			virtual task_base& get_task() const { return *task; }

		public:
			pooled_task_holder(uint32_t period_, task_pool &pool_,
								const std::shared_ptr<task_base> &task_)
				: task_holder(period_, task_->get_hash()), pool(pool_), task(task_)
			{ }

			virtual std::shared_ptr<task_base> get_ptr() { return task; }
		};

		template<typename Head>
		inline size_t hash(const Head &head) { return std::hash<Head>()(head); }

		template<typename Head, typename... Tail>
		inline size_t hash(const Head &head, const Tail &... tail)
		{ return std::hash<Head>()(head) + 47 * hash(tail...); }
	}

	/**
	 * Task container, which manages the running of tasks.  task_base tasks are registered with
	 * the register_simple_task() function, while for thread_task tasks we have
	 * register_threaded_task(). You pass the class name as the template parameter, and any
	 * additional parameters to the constructor as function parameters. The period parameter
	 * specifies how often the task will run - the task is run on every period-th invocation of
	 * run_all_tasks().  these functions return either a pointer to the newly created object or
	 * to a previously registered task, if there is an equivalent one.  The tasks will be run
	 * only as long as someone holds the object pointer.
	 */
	template<typename Task = task_base>
	class task_container {
		typedef std::unique_ptr<priv::task_holder> holder_ptr;
		typedef std::unordered_set<holder_ptr, size_t (*)(const holder_ptr &),
								   bool (*)(const holder_ptr &, const holder_ptr &)>
		Tasks;

		semaphore sem_wait;
		Tasks tasks;
		priv::task_pool pool;

		static size_t get_hash(const holder_ptr &h) { return h->hash; }
		static bool is_equal(const holder_ptr &a, const holder_ptr &b)
		{
			if(get_hash(a) != get_hash(b))
				return false;

			if(typeid(*a) != typeid(*b))
				return false;

			return *a == *b;
		}


		std::shared_ptr<task_base> register_holder(std::unique_ptr<priv::task_holder> &&h)
		{
			const auto &p = tasks.insert(std::move(h));

			if(not p.second)
				(*p.first)->merge(std::move(*h));

			return (*p.first)->get_ptr();
		}

		std::shared_ptr<task_base>
		do_register_task(uint32_t period, const std::shared_ptr<task_base> &task)
		{
			return register_holder(std::unique_ptr<priv::task_holder>(
						new priv::pooled_task_holder(period, pool, task)
					));
		}

		std::shared_ptr<task_base>
		do_register_thread(uint32_t period, const std::shared_ptr<thread_task> &task)
		{
			return register_holder(std::unique_ptr<priv::task_holder>(
						new priv::single_task_holder(period, task)
					));
		}

	public:
		// TODO put some intelligence into choosing pool size ?
		task_container()
			: tasks(1, get_hash, is_equal), pool(2)
		{ }

		template<typename Task_, typename... Params>
		std::shared_ptr<Task_> register_simple_task(uint32_t period, Params&&... params)
		{
			static_assert(std::is_base_of<Task, Task_>::value,
					"Task_ must be derived from Task");
			static_assert(std::is_base_of<task_base, Task_>::value,
					"Task_ must be derived from task_base");

			return std::dynamic_pointer_cast<Task_>(do_register_task(period,
						std::shared_ptr<task_base>(new Task_(std::forward<Params>(params)...))
					));
		}

		template<typename Task_, typename... Params>
		std::shared_ptr<Task_> register_threaded_task(uint32_t period, Params&&... params)
		{
			static_assert(std::is_base_of<Task, Task_>::value,
					"Task_ must be derived from Task");
			static_assert(std::is_base_of<thread_task, Task_>::value,
					"Task_ must be derived from thread_task");

			return std::dynamic_pointer_cast<Task_>(do_register_thread(period,
						std::shared_ptr<thread_task>(new Task_(std::forward<Params>(params)...))
					));
		}

		void run_all_tasks();
	};

	template<typename Thread>
	void task_container<Thread>::run_all_tasks()
	{
		for(auto i = tasks.begin(); i != tasks.end(); ) {
			if((*i)->tick())
				++i;
			else {
				auto t = i++;
				tasks.erase(t);
			}
		}

		pool.wait();
	}
}

#endif /* THREAD_HH */
