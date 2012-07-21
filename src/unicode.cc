/* -*- mode: c++; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: t -*-
 * vim: ts=4 sw=4 noet ai cindent syntax=cpp
 *
 * Conky, a system monitor, based on torsmo
 *
 * Please see COPYING for details
 *
 * Copyright (C) 2012 Pavel Labath et al.
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

#include "unicode.hh"

#if BYTE_ORDER == BIG_ENDIAN
#define UTF32 "UTF-32BE"
#else
#define UTF32 "UTF-32LE"
#endif

std::locale unicode_converter::locale(std::locale::classic(), new codecvt);
const unicode_converter::codecvt &unicode_converter::converter = std::use_facet<codecvt>(locale);

unicode_converter::unicode_converter()
	: state("UTF-8", UTF32)
{ }

std::string unicode_converter::to_utf8(const std::u32string &in)
{
	std::string out;
	out.resize(in.length()*6);
	const char32_t *inend;
	char *outend;

	if(converter.in(state, in.c_str(), in.c_str()+in.length(), inend,
				&*out.begin(), &*out.end(), outend) != codecvt::ok) {
		throw conversion_error("UTF8");
	}

	out.resize(outend-out.c_str());
	return out;
}

std::u32string unicode_converter::to_utf32(const std::string &in)
{
	std::u32string out;
	out.resize(in.length());
	const char *inend;
	char32_t *outend;

	if(converter.out(state, in.c_str(), in.c_str()+in.length(), inend,
				&*out.begin(), &*out.end(), outend) != codecvt::ok) {
		throw conversion_error("UTF32");
	}

	out.resize(outend-out.c_str());
	return out;
}
