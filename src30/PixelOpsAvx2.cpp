// ImageWalker by Zac Walker
// Implements AVX2 BGRA blending; callers must complete CPU and OS runtime checks first.

#include "PixelOps.h"

#include <immintrin.h>

namespace iw::ui
{
	namespace
	{
		__m256i divide_by_255(const __m256i value)
		{
			return _mm256_srli_epi16(_mm256_add_epi16(_mm256_add_epi16(value, _mm256_set1_epi16(1)),
			                                                       _mm256_srli_epi16(value, 8)), 8);
		}
	}

	void blend_constant_bgra_avx2(const std::span<std::uint32_t> pixels, const color foreground)
	{
		const __m256i zero = _mm256_setzero_si256();
		const __m256i alpha = _mm256_set1_epi16(foreground.a);
		const __m256i inverse = _mm256_set1_epi16(255 - foreground.a);
		const __m256i rounding = _mm256_set1_epi16(127);
		const __m256i foregroundWords = _mm256_setr_epi16(
			foreground.b, foreground.g, foreground.r, 255,
			foreground.b, foreground.g, foreground.r, 255,
			foreground.b, foreground.g, foreground.r, 255,
			foreground.b, foreground.g, foreground.r, 255);
		const __m256i opaque = _mm256_set1_epi32(static_cast<int>(0xff000000));
		size_t index = 0;
		for (; index + 8 <= pixels.size(); index += 8)
		{
			const __m256i background = _mm256_loadu_si256(
				reinterpret_cast<const __m256i*>(pixels.data() + index));
			const auto blend = [&](const __m256i words)
			{
				const __m256i sum = _mm256_add_epi16(
					_mm256_add_epi16(_mm256_mullo_epi16(words, inverse),
					                 _mm256_mullo_epi16(foregroundWords, alpha)), rounding);
				return divide_by_255(sum);
			};
			const __m256i result = _mm256_or_si256(
				_mm256_packus_epi16(blend(_mm256_unpacklo_epi8(background, zero)),
				                      blend(_mm256_unpackhi_epi8(background, zero))), opaque);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(pixels.data() + index), result);
		}
		blend_constant_bgra(pixels.subspan(index), foreground, PixelBackend::scalar);
	}

	void interpolate_bgra_avx2(const std::span<const std::uint32_t> first,
	                           const std::span<const std::uint32_t> second,
	                           const std::span<std::uint32_t> destination, const std::uint16_t fraction)
	{
		const __m256i zero = _mm256_setzero_si256();
		const __m256i secondWeight = _mm256_set1_epi16(fraction);
		const __m256i firstWeight = _mm256_set1_epi16(256 - fraction);
		const __m256i rounding = _mm256_set1_epi16(128);
		size_t index = 0;
		for (; index + 8 <= destination.size(); index += 8)
		{
			const __m256i firstPixels = _mm256_loadu_si256(
				reinterpret_cast<const __m256i*>(first.data() + index));
			const __m256i secondPixels = _mm256_loadu_si256(
				reinterpret_cast<const __m256i*>(second.data() + index));
			const auto interpolate = [&](const __m256i firstWords, const __m256i secondWords)
			{
				return _mm256_srli_epi16(_mm256_add_epi16(
					_mm256_add_epi16(_mm256_mullo_epi16(firstWords, firstWeight),
					                 _mm256_mullo_epi16(secondWords, secondWeight)), rounding), 8);
			};
			const __m256i result = _mm256_packus_epi16(
				interpolate(_mm256_unpacklo_epi8(firstPixels, zero), _mm256_unpacklo_epi8(secondPixels, zero)),
				interpolate(_mm256_unpackhi_epi8(firstPixels, zero), _mm256_unpackhi_epi8(secondPixels, zero)));
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(destination.data() + index), result);
		}
		interpolate_bgra(first.subspan(index), second.subspan(index), destination.subspan(index), fraction,
		                 PixelBackend::scalar);
	}
}