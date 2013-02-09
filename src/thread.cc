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

#include "logging.h"

namespace conky {
	namespace priv {
		bool task_holder::tick()
		{
			if(remaining-- == 0) {
				if(!unique() || ++unused < UNUSED_MAX) {
					remaining = period-1;
					run();
				}
			}
			return unused < UNUSED_MAX;
		}

		void task_holder::merge(task_holder &&other)
		{
			if(other.period < period) {
				period = other.period;
				remaining = 0;
			}
			unused = 0;
		}

		void single_task_holder::run()
		{
			if(not thread)
				thread = new std::thread(&thread_task::work, std::ref(*task));

			task->tick();
		}

		single_task_holder::~single_task_holder()
		{
			if(thread) {
				task->stop();
				thread->join();
				delete thread;
				thread = NULL;
			}
		}

		void task_pool::runner()
		{
			while(true) {
				std::shared_ptr<task_base> task;
				{
					std::unique_lock<std::mutex> lock(mutex);
					if(done)
						break;
					if(queue.empty()) {
						cv.wait(lock);
						continue;
					} else {
						task = queue.front();
						queue.pop();
					}
				}
				task->tick();
				sem.post();
			}
		}

		task_pool::~task_pool()
		{
			{
				std::lock_guard<std::mutex> lock(mutex);
				done = true;
				cv.notify_all();
			}
			for(auto i = threads.begin(); i != threads.end(); ++i)
				i->join();
		}
	}

	piped_thread::signal piped_thread::get_signal()
	{
		char s;
		if(read(signalfd(), &s, 1) != 1)
			throw std::runtime_error("thread_base: unable to read signal.");
		switch(s) {
			case 'X': return DONE;
			case 'T': return NEXT;
			default: throw std::logic_error("thread_base: Unknown signal.");
		}
	}

	void piped_thread::tick()
	{
		Base::tick();

		if(write(pipefd.second, "T", 1) != 1)
			NORM_ERR("Unable to signal thread tick. Is the thread stuck?");
	}

	void piped_thread::stop()
	{
		fcntl_setfl(pipefd.second, fcntl_getfl(pipefd.second) & ~O_NONBLOCK);
		if(write(pipefd.second, "X", 1) != 1)
			throw std::runtime_error("thread_base: unable to signal thread to terminate.");

		Base::stop();
	}

	piped_thread::~piped_thread()
	{
		close(pipefd.first);
		close(pipefd.second);
		pipefd = { -1, -1 };
	}
}

