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

#include "config.h"

#include "thread.hh"

#include <unistd.h>
#include <typeinfo>

namespace conky {
	namespace {
		enum {UNUSED_MAX = 5};
	}

	thread_base::thread_base(size_t hash_, uint32_t period_, bool wait_, bool use_pipe)
		: thread(NULL), hash(hash_), period(period_), remaining(0),
		  pipefd(use_pipe ? pipe2(O_CLOEXEC) : std::pair<int, int>(-1, -1)),
		  wait(wait_), done(false), unused(0)
	{
		if(use_pipe)
			fcntl_setfl(pipefd.second, fcntl_getfl(pipefd.second) | O_NONBLOCK);
	}

	thread_base::~thread_base()
	{
		stop();
	}

	void thread_base::run(semaphore &sem_wait)
	{
		if(not thread)
			thread = new std::thread(&thread_base::start_routine, this, std::ref(sem_wait));

		sem_start.post();
		if(pipefd.second >= 0)
			write(pipefd.second, "T", 1);
	}

	void thread_base::start_routine(semaphore &sem_wait)
	{
		for(;;) {
			sem_start.wait();
			if(done)
				return;

			// clear any remaining posts in case the previous iteration was very slow
			// (this should only happen if wait == false)
			while(sem_start.trywait());

			work();
			if(wait)
				sem_wait.post();
		}
	}

	void thread_base::stop()
	{
		if(thread) {
			done = true;
			sem_start.post();
			if(pipefd.second >= 0) {
				fcntl_setfl(pipefd.second, fcntl_getfl(pipefd.second) & ~O_NONBLOCK);
				write(pipefd.second, "X", 1);
			}
			thread->join();
			delete thread;
			thread = NULL;
		}
		if(pipefd.first >= 0) {
			close(pipefd.first);
			pipefd.first = -1;
		}
		if(pipefd.second >= 0) {
			close(pipefd.second);
			pipefd.second = -1;
		}
	}

	bool thread_container::is_equal(const handle &a, const handle &b)
	{
		if(a->hash != b->hash)
			return false;

		if(typeid(*a) != typeid(*b))
			return false;

		return *a == *b;
	}

	void thread_base::merge(thread_base &&other)
	{
		if(other.period < period) {
			period = other.period;
			remaining = 0;
		}
		assert(wait == other.wait);
		unused = 0;
	}

	thread_base::signal thread_base::get_signal()
	{
		char s;
		read(signalfd(), &s, 1);
		switch(s) {
			case 'X': return DONE;
			case 'T': return NEXT;
			default: throw std::logic_error("thread_base: Unknown signal.");
		}
	}

	thread_container::handle thread_container::do_register_thread(const handle &h)
	{
		const auto &p = threads.insert(h);

		if(not p.second)
			(*p.first)->merge(std::move(*h));

		return *p.first;
	}


	void thread_container::run_all_threads()
	{
		size_t wait = 0;
		for(auto i = threads.begin(); i != threads.end(); ) {
			thread_base &thr = **i;

			if(thr.remaining-- == 0) {
				if(!i->unique() || ++thr.unused < UNUSED_MAX) {
					thr.remaining = thr.period-1;
					thr.run(sem_wait);
					if(thr.wait)
						++wait;
				}
			}
			if(thr.unused == UNUSED_MAX) {
				auto t = i;
				++i;
				threads.erase(t);
			} else 
				++i;
		}

		while(wait-- > 0)
			sem_wait.wait();
	}
}

