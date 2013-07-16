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

#include <config.h>

#include "util.hh"

#include <cinttypes>

#include "c++wrap.hh"

namespace conky {
    std::string format_seconds(std::chrono::seconds seconds)
    {
        uint64_t rest = seconds.count();
        unsigned s = rest % 60;
        rest /= 60;
        unsigned m = rest % 60;
        rest /= 60;
        unsigned h = rest % 24;
        rest /= 24;

	if (rest > 0)
            return strprintf("%" PRIu64 "d %uh %um", rest, h, m);
	else
            return strprintf("%uh %um %us", h, m, s);
    }
}
