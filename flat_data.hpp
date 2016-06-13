#pragma once

#include <limits>

#include "buffer.hpp"

namespace jup {

/**
 * This is a 'flat' data structure, meaning that it goes into a Buffer together
 * with its contents. It has the following layout:
 * If start != 0:
 *   ... start ... size element1 element2 ...
 *         |        ^
 *         +--------+
 * Else the object is invalid, and any method other than init() has undefined
 * behaviour.
 *
 * Usage: Initialize this with the containing Buffer (using the contructor or
 * init()). Then, add the elements via push_back. Remember that each push_back
 * may invalidate all pointers into the Buffer including the pointer/reference
 * you use for the Flat_array! Circumvent this either by getting a new pointer
 * every time, or by reseving the memory that is needed (it then is also
 * recommended to set the trap_alloc() flag on the Buffer during
 * insertion). After initialization is finished, this object should be
 * considered read-only; new elements can only be appended at the end of the
 * Buffer (which has to be exactly the end of the Flat_array as well).
 */
template <typename T, typename _Offset_t = u16, typename _Size_t = u8>
struct Flat_array {
	using Type = T;
	using Offset_t = _Offset_t;
	using Size_t = _Size_t;
	
	Offset_t start;
		
	Flat_array(): start{0} {}
	Flat_array(Buffer* containing) { init(containing); }

	/**
	 * Initializes the Flat_array by having it point to the end of the
	 * Buffer. The object must be contained in the Buffer!
	 */
	void init(Buffer* containing) {
		assert(containing);
		assert((void*)containing->begin() <= (void*)this
			   and (void*)this < (void*)containing->end());
		narrow(start, containing->end() - (char*)this);
		containing->emplace_back<Size_t>();
	}

	void init(Flat_array<Type, Offset_t, Size_t> const& orig, Buffer* containing) {
		Size_t size = orig.m_size();
		assert(containing);
		assert((void*)containing->begin() <= (void*)this
			and (void*)this < (void*)containing->end());
		narrow(start, containing->end() - (char*)this);
		containing->append(orig.begin(), sizeof(Size_t) + size * sizeof(T));
	}

	Size_t size() const {
        if (!start) return 0;
        return m_size();
    }

	T* begin() { return start ? (T*)(&m_size() + 1) : nullptr; }
	T* end()   { return begin() + size(); }
	T const* begin() const { return start ? (T const*)(&m_size() + 1) : nullptr; }
	T const* end()   const { return begin() + size(); }

    T& front() {assert(size()); return *begin();}
    T& back()  {assert(size()); return end()[-1];}
    T const& front() const {assert(size()); return *begin();}
    T const& back()  const {assert(size()); return end()[-1];}

	/**
	 * Return the element. Does bounds-checking.
	 */
	T& operator[] (int pos) {
		assert(0 <= pos and pos < size());
		return *(begin() + pos);
	}
	T const& operator[] (int pos) const {
		assert(0 <= pos and pos < size());
		return *(begin() + pos);
	}

	T& emplace_back(Buffer* containing) {
		assert(containing);
		assert((void*)containing->begin() <= (void*)this
			and (void*)end() == (void*)containing->end());
		++m_size();
		return containing->emplace_back<T>();
	}

	/**
	 * Insert an element at the back. The end of the list and the end of the
	 * Buffer must be the same! This operation may invalidate all pointers to
	 * the Buffer, including the one you use for this object!
	 */
	void push_back(T const& obj, Buffer* containing) {
		assert(containing);
		assert((void*)containing->begin() <= (void*)this
			   and (void*)end() == (void*)containing->end());
		containing->emplace_back<T>(obj);
		++m_size();
	}

	/**
	 * Count the number of objects equal to obj that are contained in this
	 * array.
	 */
	int count(T const& obj) const {
		int result = 0;
		for (auto& i: *this)
			if (i == obj) ++result;
		return result;
	}

private:
	Size_t& m_size() {
		assert(start);
		return *(Size_t*)(((char*)this) + start);
	}
	Size_t const& m_size() const {
		assert(start);
		return *(Size_t const*)(((char*)this) + start);
	}
};

/**
 * This is a 'flat' data structure, meaning that it goes into a Buffer together
 * with its contents. It has the following layout:
 *   ... map ... obj1 ... obj2 ...
 * where map is an array of offsets pointing to the strings. The obj need not be
 * continuous, the buffer can be used for other purposes in between.

 * This is a hashtable; it supports O(1) lookup and insertion (as long as it is
 * not too full). This hashtable specifically maps objects to integers. It is
 * guaranteed that an empty object is mapped to the id 0 if it is the first
 * object inserted. The inserted objects can be heterogeneous, but they are
 * compared by comparing their bytes.
 *
 * The template paramter add_zero causes a 0 to be appended after each block of
 * data containing an object. This is intended for the use with strings.
 */
template <typename Offset_t = u16, typename Id_t = u8, int Size = 256,
		  bool add_zero = true>
struct Flat_idmap_base {
	Offset_t map[Size];
	
	Flat_idmap_base(): map{0} {
		static_assert(Size <= std::numeric_limits<Id_t>::max() + 1,
					  "Id_t is not big enough for Size");
	}

	/**
	 * Return the id associated with the object obj. If it does not already
	 * exists the behaviour is undefined.
	 */
	Id_t get_id(Buffer_view obj) const {
		Id_t orig_id = obj.get_hash() % Size;
		Id_t id = orig_id;
		while (map[id] and obj != get_value(id)) {
			id = (id + 1) % Size;
			assert(id != orig_id);
		}
		assert(map[id]);
		return id;
	}

	/**
	 * Return the id associated with the object obj. If it does not already
	 * exists, it is inserted. The Buffer must contain the Flat_idmap object.
	 */
	Id_t get_id(Buffer_view obj, Buffer* containing) {
		assert(containing);
		assert((void*)containing->begin() <= (void*)this
			   and (void*)this < (void*)containing->end());
		
		Id_t orig_id = obj.get_hash() % Size;
		Id_t id = orig_id;
		while (map[id] and obj != get_value(id)) {
			id = (id + 1) % Size;
			assert(id != orig_id);
		}
		if (!map[id]) {
			map[id] = containing->end() - (char*)this;
			containing->append(Buffer_view::from_obj(obj.size()));
			containing->append(obj);
			if (add_zero) {
				containing->append({"", 1});
			}
		}
		return id;
	}

	/**
	 * Return the value of an id. If it does not already exists the behaviour is
	 * undefined.
	 */
	Buffer_view get_value(Id_t id) const {
		assert(0 <= id and id < Size and map[id]);
		return {(char*)this + map[id] + sizeof(int),
				*(int*)((char*)this + map[id])};
	}

};

using Flat_idmap = Flat_idmap_base<>;

/**
 * This is a 'flat' data structure, meaning that it goes into a Buffer together
 * with its contents. It has the following layout:
 * If start != 0:
 *   ... start ... obj ...
 *         |        ^
 *         +--------+
 * Else the object is invalid, and any method other than init() has undefined
 * behaviour.
 *
 * This models a reference, but is accessed like a pointer (e.g. using *, ->)
 */
template <typename T, typename _Offset_t = u16>
struct Flat_ref {
	using Type = T;
	using Offset_t = _Offset_t;

	Offset_t start;

	Flat_ref(): start{0} {}
	
	template <typename _T>
	Flat_ref(_T const& obj, Buffer* containing) {
		init(obj, containing);
	}

	/**
	 * Initializes the Flat_ref by having it point to the end of the Buffer. The
	 * object must be contained in the Buffer!
	 */
	template <typename _T>
	void init(_T const& obj, Buffer* containing) {
		assert(containing);
		assert((void*)containing->begin() <= (void*)this
			   and (void*)this < (void*)containing->end());
		narrow(start, containing->end() - (char*)this);
		containing->emplace_back<_T>(obj);
	}	

	T* ptr() {
		assert(start);
		return (T*)((char*)this + start);
	}
	T const* ptr() const {
		assert(start);
		return (T const*)((char*)this + start);
	}
	T* operator->() { return ptr(); }
	T const* operator->() const { return ptr(); }
	T& operator* () { return *ptr(); }
	T const& operator* () const { return *ptr(); }	
};

} /* end of namespace jup */
