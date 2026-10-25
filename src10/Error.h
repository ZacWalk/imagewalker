#pragma once

// One exception type for the whole app. The MFC-shaped hierarchy this replaced
// had no std::exception base, so a catch on that base silently caught nothing.

#include <stdexcept>

namespace IW
{
	class Error : public std::runtime_error
	{
	public:
		explicit Error(const char* szWhat) : std::runtime_error(szWhat)
		{
		}
	};

	inline void Throw(const char* szWhat) { throw Error(szWhat); }
	inline void ThrowOutOfMemory() { throw std::bad_alloc(); }
}
