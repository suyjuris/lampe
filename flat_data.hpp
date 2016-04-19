#pragma once

#include <limits>

#include "buffer.hpp"

namespace jup {

template <typename _T, typename _Offset_t = u16, typename _Size_t = u8>
struct Flat_array {
	using T = _T;
	using Offset_t = _Offset_t;
	using Size_t = _Size_t;
	
	Offset_t start;

	Flat_array(): start{0} {}
	Flat_array(Buffer* containing) { init(containing); }

	void init(Buffer* containing) {
		assert(containing);
		assert((void*)containing->begin() <= (void*)this
			   and (void*)this < (void*)containing->end());
		auto diff = containing->end() - (char*)this;
		assert(diff < std::numeric_limits<Offset_t>::max());
		start = containing->end() - (char*)this;
		containing->emplace_ref<Size_t>(containing->size());
	}

	Size_t size() const { return m_size(); }

	T* begin() { return (T*)(&m_size() + 1); }
	T* end()   { return begin() + size(); }
	T const* begin() const { return (T const*)(&m_size() + 1); }
	T const* end()   const { return begin() + size(); }
	
	T& operator[] (int pos) {
		assert(0 <= pos and pos < size());
		return *(begin() + pos);
	}
	T const& operator[] (int pos) const {
		assert(0 <= pos and pos < size());
		return *(begin() + pos);
	}

	void push_back(T const& obj, Buffer* containing) {
		assert(containing);
		assert((void*)containing->begin() <= (void*)this
			   and (void*)end() == (void*)containing->end());
		containing->emplace_ref<T>(containing->size(), obj);
		++m_size();
	}

	int count(T const& obj) const {
		int result = 0;
		for (auto& i: *this)
			if (i == obj) ++result;
		return result;
	}

private:
	Size_t& m_size() {return *(Size_t*)(((char*)this) + start);}
	Size_t const& m_size() const {
		return *(Size_t const*)(((char*)this) + start);
	}
};

template <typename Offset_t = u16, typename Id_t = u8, int Size = 256>
struct Flat_idmap_base {
	Offset_t map[Size];
	
	Flat_idmap_base(): map{0} {
		static_assert(Size <= std::numeric_limits<Id_t>::max() + 1,
					  "Id_t is not big enough for Size");
	}

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
		}
		return id;
	}

	Buffer_view get_value(Id_t id) const {
		assert(0 <= id and id < Size and map[id]);
		return {(char*)this + map[id] + sizeof(int),
				*(int*)((char*)this + map[id])};
	}

};

using Flat_idmap = Flat_idmap_base<>;

} /* end of namespace jup */
