// ImageWalker by Zac Walker
// Implements scalar and SSE2 BGRA blending with safe runtime dispatch to AVX2.

#include "Platform.h"
#include "PixelOps.h"

#include <algorithm>
#include <intrin.h>

namespace iw::ui
{
	namespace
	{
		std::uint32_t blend_channel(const std::uint32_t background, const std::uint32_t foreground,
		                            const std::uint32_t alpha)
		{
			return (background * (255 - alpha) + foreground * alpha + 127) / 255;
		}

		void blend_scalar(std::span<std::uint32_t> pixels, const color foreground)
		{
			for (auto& pixel : pixels)
				pixel = blend_channel(pixel & 0xff, foreground.b, foreground.a) |
					(blend_channel((pixel >> 8) & 0xff, foreground.g, foreground.a) << 8) |
					(blend_channel((pixel >> 16) & 0xff, foreground.r, foreground.a) << 16) | 0xff000000;
		}

		__m128i divide_by_255(const __m128i value)
		{
			return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(value, _mm_set1_epi16(1)),
			                                           _mm_srli_epi16(value, 8)), 8);
		}

		void blend_sse2(std::span<std::uint32_t> pixels, const color foreground)
		{
			const __m128i zero = _mm_setzero_si128();
			const __m128i alpha = _mm_set1_epi16(foreground.a);
			const __m128i inverse = _mm_set1_epi16(255 - foreground.a);
			const __m128i rounding = _mm_set1_epi16(127);
			const __m128i foregroundWords = _mm_setr_epi16(
				foreground.b, foreground.g, foreground.r, 255,
				foreground.b, foreground.g, foreground.r, 255);
			const __m128i opaque = _mm_set1_epi32(static_cast<int>(0xff000000));
			size_t index = 0;
			for (; index + 4 <= pixels.size(); index += 4)
			{
				const __m128i background = _mm_loadu_si128(
					reinterpret_cast<const __m128i*>(pixels.data() + index));
				const auto blend = [&](const __m128i words)
				{
					const __m128i sum = _mm_add_epi16(
						_mm_add_epi16(_mm_mullo_epi16(words, inverse),
						              _mm_mullo_epi16(foregroundWords, alpha)), rounding);
					return divide_by_255(sum);
				};
				const __m128i result = _mm_or_si128(
					_mm_packus_epi16(blend(_mm_unpacklo_epi8(background, zero)),
					                 blend(_mm_unpackhi_epi8(background, zero))), opaque);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(pixels.data() + index), result);
			}
			blend_scalar(pixels.subspan(index), foreground);
		}

		void interpolate_scalar(const std::span<const std::uint32_t> first,
		                        const std::span<const std::uint32_t> second,
		                        const std::span<std::uint32_t> destination, const std::uint16_t fraction)
		{
			const std::uint32_t inverse = 256 - fraction;
			for (size_t index = 0; index < destination.size(); ++index)
			{
				std::uint32_t pixel{};
				for (int shift = 0; shift < 32; shift += 8)
					pixel |= (((first[index] >> shift & 0xff) * inverse +
						(second[index] >> shift & 0xff) * fraction + 128) >> 8) << shift;
				destination[index] = pixel;
			}
		}

		void interpolate_sse2(const std::span<const std::uint32_t> first,
		                      const std::span<const std::uint32_t> second,
		                      const std::span<std::uint32_t> destination, const std::uint16_t fraction)
		{
			const __m128i zero = _mm_setzero_si128();
			const __m128i secondWeight = _mm_set1_epi16(fraction);
			const __m128i firstWeight = _mm_set1_epi16(256 - fraction);
			const __m128i rounding = _mm_set1_epi16(128);
			size_t index = 0;
			for (; index + 4 <= destination.size(); index += 4)
			{
				const __m128i firstPixels = _mm_loadu_si128(
					reinterpret_cast<const __m128i*>(first.data() + index));
				const __m128i secondPixels = _mm_loadu_si128(
					reinterpret_cast<const __m128i*>(second.data() + index));
				const auto interpolate = [&](const __m128i firstWords, const __m128i secondWords)
				{
					return _mm_srli_epi16(_mm_add_epi16(
						_mm_add_epi16(_mm_mullo_epi16(firstWords, firstWeight),
						              _mm_mullo_epi16(secondWords, secondWeight)), rounding), 8);
				};
				const __m128i result = _mm_packus_epi16(
					interpolate(_mm_unpacklo_epi8(firstPixels, zero), _mm_unpacklo_epi8(secondPixels, zero)),
					interpolate(_mm_unpackhi_epi8(firstPixels, zero), _mm_unpackhi_epi8(secondPixels, zero)));
				_mm_storeu_si128(reinterpret_cast<__m128i*>(destination.data() + index), result);
			}
			interpolate_scalar(first.subspan(index), second.subspan(index), destination.subspan(index), fraction);
		}
	}

	void blend_constant_bgra_avx2(std::span<std::uint32_t> pixels, color foreground);
	void interpolate_bgra_avx2(std::span<const std::uint32_t> first,
	                           std::span<const std::uint32_t> second,
	                           std::span<std::uint32_t> destination, std::uint16_t fraction);

	void blend_constant_bgra(const std::span<std::uint32_t> pixels, const color foreground,
	                         const PixelBackend backend)
	{
		if (pixels.empty() || foreground.a == 0) return;
		if (foreground.a == 255)
		{
			const std::uint32_t value = foreground.b | (foreground.g << 8) |
				(foreground.r << 16) | 0xff000000;
			std::ranges::fill(pixels, value);
			return;
		}
		if ((backend == PixelBackend::automatic && platform::has_avx2()) || backend == PixelBackend::avx2)
		{
			blend_constant_bgra_avx2(pixels, foreground);
			return;
		}
		if ((backend == PixelBackend::automatic && platform::has_sse2()) || backend == PixelBackend::sse2)
		{
			blend_sse2(pixels, foreground);
			return;
		}
		blend_scalar(pixels, foreground);
	}

	void interpolate_bgra(std::span<const std::uint32_t> first, std::span<const std::uint32_t> second,
	                      std::span<std::uint32_t> destination, const std::uint16_t fraction,
	                      const PixelBackend backend)
	{
		const size_t count = (std::min)({first.size(), second.size(), destination.size()});
		first = first.first(count);
		second = second.first(count);
		destination = destination.first(count);
		if (count == 0) return;
		if ((backend == PixelBackend::automatic && platform::has_avx2()) || backend == PixelBackend::avx2)
		{
			interpolate_bgra_avx2(first, second, destination, fraction);
			return;
		}
		if ((backend == PixelBackend::automatic && platform::has_sse2()) || backend == PixelBackend::sse2)
		{
			interpolate_sse2(first, second, destination, fraction);
			return;
		}
		interpolate_scalar(first, second, destination, fraction);
	}
}