
#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace jup {

struct Buffer;

struct Buffer_view {
	Buffer_view(void const* data = nullptr, int size = 0):
		m_data{data}, m_size{size} {assert(size >= 0);}
	
	Buffer_view(Buffer const& buf);
	
	template<typename T>
	Buffer_view(std::vector<T> const& vec):
		Buffer_view{vec.data(), (int)(vec.size() * sizeof(T))} {}
	
	template<typename T>
	Buffer_view(std::basic_string<T> const& str):
		Buffer_view{str.data(), (int)(str.size() * sizeof(T))} {}
	
	Buffer_view(char const* str):
		Buffer_view{str, (int)std::strlen(str)} {}

	template<typename T>
	static Buffer_view from_obj(T const& obj) {
		return Buffer_view {&obj, sizeof(obj)};
	}

	int size() const {return m_size;}
	
	char const* begin() const {return (char const*)m_data;}
	char const* end()   const {return (char const*)m_data + m_size;}
	char const* data()  const {return begin();}

	char const* c_str() const {
		assert(*(data() + size()) == 0);
		return data();
	}

	u32 get_hash() const {
		u32 result = 0;
		for (char c: *this) {
			result = (result * 33) ^ c;
		}
		return result;
	}

	bool operator== (Buffer_view const& buf) const {
		if (size() != buf.size()) return false;
		for (int i = 0; i < size(); ++i) {
			if (data()[i] != buf.data()[i]) return false;
		}
		return true;
	}
	bool operator!= (Buffer_view const& buf) const { return !(*this == buf); }
	
	void const* const m_data;
	int const m_size;
};
	
class Buffer {
#ifndef NDEBUG
	static_assert(sizeof(int) == 4, "Assuming 32bit ints for the bitmasks.");
#endif
public:
	Buffer() {}
	Buffer(Buffer const& buf) { append(buf); }
	Buffer(Buffer&& buf) {
		m_data = buf.m_data;
		m_size = buf.m_size;
		m_capacity = buf.m_capacity;
		buf.m_data = nullptr;
		buf.m_size = 0;
		buf.m_capacity = 0;
	}
	~Buffer() { free(); }
	Buffer& operator= (Buffer const& buf) {
		reset();
		append(buf);
		return *this;
	}
	Buffer& operator= (Buffer&& buf) {
		std::swap(m_data, buf.m_data);
		std::swap(m_size, buf.m_size);
		std::swap(m_capacity, buf.m_capacity);
		return *this;
	}

	void reserve(int newcap) {
		if (capacity() < newcap) {
			newcap = std::max(newcap, capacity() * 2);
			assert(!trap_alloc());
			if (m_data) {
				m_data = (char*)std::realloc(m_data, newcap);
			} else {
				m_data = (char*)std::malloc(newcap);
			}
			/* the trap_alloc flag is stored in m_capacity, don't disturb it */
			m_capacity += newcap - capacity();
			assert(m_data);
		}
	}

	void append(void const* buf, int buf_size) {
		if (!buf_size) return;
		assert(buf_size > 0 and buf);
		if (capacity() < m_size + buf_size)
			reserve(m_size + buf_size);
		
		assert(capacity() >= m_size + buf_size);
		std::memcpy(m_data + m_size, buf, buf_size);
		m_size += buf_size;
	}
	void append(Buffer_view buffer) {
		append(buffer.data(), buffer.size());
	}

	void resize(int nsize) {
		m_size = nsize;
		assert(m_size >= 0);
		reserve(m_size);
	}
	void addsize(int incr) {
		resize(m_size + incr);
	}
	
	void reset() {
		m_size = 0;
	}
	
	void free() {
		assert(!trap_alloc());
		std::free(m_data);
		m_data = nullptr;
		m_size = 0;
		m_capacity = 0;
	}

	int size() const { return m_size; }
	int capacity() const {
#ifndef NDEBUG
		return m_capacity & 0x7fffffff;
#else
		return m_capacity;
#endif
	}
	
	bool trap_alloc() const {
#ifndef NDEBUG
		return (u32)m_capacity >> 31;
#else
		return false;
#endif
	}
	bool trap_alloc(bool value) {
#ifndef NDEBUG
		m_capacity ^= (u32)(trap_alloc() ^ value) << 31;
#endif					   
		return trap_alloc();
	}
	
	int space() const {return capacity() - m_size;}
	void reserve_space(int atleast) {
		reserve(m_size + atleast);
	}
	
	template <typename T>
	T& get_ref(int offset = 0) {
		reserve(offset + sizeof(T));
		return *(T*)(m_data + offset);
	}
	template <typename T, typename... Args>
	T& emplace_ref(int offset = 0, Args&&... args) {
		int end = offset + sizeof(T);
		reserve(end);
		if (m_size < end) resize(end);
		return *(new(m_data + offset) T {std::forward<Args>(args)...});
	}
	template <typename T, typename... Args>
	T& emplace_back(Args&&... args) {
		return emplace_ref<T>(size(), std::forward<Args>(args)...);
	}

	char* begin() {return m_data;}
	char* end()   {return m_data + m_size;}
	char* data()  {return begin();}
	char const* begin() const {return m_data;}
	char const* end()   const {return m_data + m_size;}
	char const* data()  const {return begin();}

private:
	char* m_data = nullptr;
	int m_size = 0, m_capacity = 0;
};

inline Buffer_view::Buffer_view(Buffer const& buf):
	Buffer_view{buf.data(), buf.size()} {}

} /* end of namespace jup */
