// ImageWalker by Zac Walker
// Portable packet-copy editing; only the caller can publish the staged bytes.

#pragma once

#include "Files.h"

namespace iw::media::edit
{
	struct Draft
	{
		std::int64_t first{};
		std::int64_t last{}; // zero keeps the end
		int turn{};          // quarter turns clockwise
		bool silent{};
		bool operator==(const Draft&) const = default;
		bool changed() const { return *this != Draft{}; }
	};

	struct Info
	{
		std::int64_t duration{};
		bool video{};
		bool audio{};
		bool sameFormat{};
		bool mp4{};
		bool turn{};
		std::wstring error;
	};

	struct Result
	{
		std::int64_t actualFirst{};
		std::wstring error;
		explicit operator bool() const { return error.empty(); }
	};

	Info inspect(const std::filesystem::path& localFile, std::wstring_view extension, const std::stop_token& stop = {});
	Result copy(const std::filesystem::path& localFile, std::wstring_view extension, const Draft& draft,
	            bool indexFirst, const files::StagedStream& output, const std::stop_token& stop = {});
}
