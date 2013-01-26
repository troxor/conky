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

#ifndef LIST_MAP_HH
#define LIST_MAP_HH

#include <cstddef>
#include <forward_list>


/*
 * A model of the Unique and Pair Associative Containers, but more lightweight than std::map.
 * The tradeoff is that we use a (singly linked, by default) list for storage of elements rather
 * than binary trees.
 */
template<typename Key, typename Data, typename Compare = std::less<Key>,
	typename Alloc = std::allocator<std::pair<const Key, Data>>>
class list_map: private std::forward_list<std::pair<const Key, Data>, Alloc> {
	typedef std::forward_list<std::pair<const Key, Data>, Alloc> Base;

public:
	typedef Key key_type;
	typedef Data data_type;
	typedef std::pair<const key_type, data_type> value_type;
	typedef value_type &reference;
	typedef const value_type &const_reference;
	typedef value_type *pointer;
	typedef const value_type *const_pointer;
	typedef ptrdiff_t difference_type;
	typedef size_t size_type;
	typedef Compare key_compare;
	class value_compare: public std::binary_function<value_type, value_type, bool> {
		friend class list_map<Key, Data, Compare, Alloc>;

		key_compare comp;
		value_compare(key_compare comp_) : comp(comp_) {}

	public:
		bool operator()(const value_type &a, const value_type &b)
		{ return comp(a.first, b.first); }
	};

private:
	value_compare value_comp_;

public:

	typedef typename Base::iterator iterator;
	typedef typename Base::const_iterator const_iterator;

	explicit list_map(const key_compare &c = key_compare())
		: Base(), value_comp_(c)
	{}

	list_map(list_map &&r) : Base(std::move(r)), value_comp_(key_compare()) { }

	explicit list_map(const std::initializer_list<value_type> &l);
	template<typename InputIterator>
	list_map(InputIterator f, const InputIterator &l, const key_compare &c = key_compare());

	const list_map& operator=(const list_map &r)
	{
		// The reinterpret_cast magic is needed, because otherwise we cannot copy const Key.
		// Without it, we would need more code and/or a more inefficient implementation.
		reinterpret_cast<std::forward_list<std::pair<Key, Data>>&>(*this) = 
			reinterpret_cast<const std::forward_list<std::pair<Key, Data>>&>(r);
		return *this;
	}
	const list_map& operator=(list_map &&r);
	const list_map& operator=(std::initializer_list<value_type> &l);

	data_type& operator[](const key_type &k)
	{
		auto i = Base::before_begin();
		auto j = Base::end();
		for(;;) {
			auto t = i++;

			if(i == j or value_comp_.comp(k, i->first)) // at end or less than the current element
				return Base::emplace_after(t, k, data_type())->second;
			else if(not value_comp_.comp(i->first, k)) // equal to the current element
				return i->second;
			// else, strictly greater than current element, continue
		}
	}

	const key_compare& key_comp() const { return value_comp_.comp; }
	const value_compare& value_comp() const { return value_comp_; }

	template<typename Pair,
		typename = typename std::enable_if<std::is_convertible<Pair, value_type>::value>::type>
	std::pair<iterator, bool> insert(Pair &&x)
	{
		auto i = Base::before_begin();
		auto j = Base::end();
		for(;;) {
			auto t = i++;

			if(i == j or value_comp_(x, *i)) // at end or less than the current element
				return { Base::insert_after(t, std::forward<Pair>(x)), true };
			else if(not value_comp_(*i, x)) // equal to the current element
				return { i, false };
			// else, strictly greater than current element, continue
		}
	}

	iterator insert(const iterator &pos, const value_type &x);
	size_type erase(const key_type &k);
	void erase(const iterator &p);
	void erase(const iterator &p, const iterator &q);
	void clear();

	iterator find(const key_type &k)
	{
		auto i = Base::before_begin();
		auto j = Base::end();
		for(;;) {
			++i;

			if(i==j or value_comp_.comp(k, i->first))
				return j;
			else if(not value_comp_.comp(i->first, k))
				return i;
		}
	}

	const_iterator find(const key_type &k) const;
	size_type count(const key_type &k);

	iterator lower_bound(const key_type &k);
	const_iterator lower_bound(const key_type &k) const;
	iterator upper_bound(const key_type &k);
	const_iterator upper_bound(const key_type &k) const;
	std::pair<iterator, iterator> equal_range(const key_type &k);
	std::pair<const_iterator, const_iterator> equal_range(const key_type &k) const;

	using Base::begin;
	using Base::end;
	size_type size() const;
	size_type max_size() const;
	bool empty() const;
	void swap(list_map &r);
};

template<typename K, typename D, typename C, typename A>
bool operator==(const list_map<K, D, C, A> &a, const list_map<K, D, C, A> &b);

template<typename K, typename D, typename C, typename A>
bool operator!=(const list_map<K, D, C, A> &a, const list_map<K, D, C, A> &b);

template<typename K, typename D, typename C, typename A>
bool operator<(const list_map<K, D, C, A> &a, const list_map<K, D, C, A> &b);

template<typename K, typename D, typename C, typename A>
bool operator>(const list_map<K, D, C, A> &a, const list_map<K, D, C, A> &b);

template<typename K, typename D, typename C, typename A>
bool operator<=(const list_map<K, D, C, A> &a, const list_map<K, D, C, A> &b);

template<typename K, typename D, typename C, typename A>
bool operator>=(const list_map<K, D, C, A> &a, const list_map<K, D, C, A> &b);

#endif /* LIST_MAP_HH */
