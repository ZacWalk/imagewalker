#include <libiw/PixelOps.h>
#include <intrin.h>

namespace iw::imaging
{
	bool has_sse2() noexcept { return true; }

	bool has_avx2() noexcept
	{
		static const bool available = []
		{
			int info[4]{};
			__cpuid(info, 0);
			if (info[0] < 7) return false;
			__cpuid(info, 1);
			constexpr int required = (1 << 27) | (1 << 28);
			if ((info[2] & required) != required || (_xgetbv(0) & 0x6) != 0x6) return false;
			__cpuidex(info, 7, 0);
			return (info[1] & (1 << 5)) != 0;
		}();
		return available;
	}
}
