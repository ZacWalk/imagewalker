#pragma once

// Scratch buffer for the scanline temporaries these apps used to take from the
// stack with alloca().
//
// Nearly every one of those sizes comes from an image header -- width, row
// bytes, palette entry count -- so a malformed file could ask for an arbitrary
// stack allocation. The storage is a std::vector; the pointer conversion is
// what lets the call sites stay as they were, since they hand the buffer to C
// APIs and index it in tight loops.

#include <vector>

namespace IW
{

template <class T>
class CBuffer
{
public:

	explicit CBuffer(size_t count) : _items(count)
	{
	}

	CBuffer(size_t count, const T &value) : _items(count, value)
	{
	}

	operator T *() { return _items.empty() ? nullptr : &_items[0]; }
	operator const T *() const { return _items.empty() ? nullptr : &_items[0]; }

	T *data() { return _items.empty() ? nullptr : &_items[0]; }
	const T *data() const { return _items.empty() ? nullptr : &_items[0]; }

	size_t size() const { return _items.size(); }
	bool empty() const { return _items.empty(); }

private:

	CBuffer(const CBuffer &);
	void operator=(const CBuffer &);

	std::vector<T> _items;
};

} // namespace IW
