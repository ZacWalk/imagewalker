// ImageWalker by Zac Walker
// Lazily resolved Windows media APIs. Missing optional OS components never prevent image browsing.

#pragma once

#include "PlatformWin32.h"

#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <memory>
#include <string_view>
#include <type_traits>

namespace iw::media::native
{
	class Api final
	{
	public:
		Api();
		~Api();
		Api(const Api&) = delete;
		Api& operator=(const Api&) = delete;

		bool available() const;
		decltype(&::MFStartup) startup{};
		decltype(&::MFShutdown) shutdown{};
		decltype(&::MFCreateAttributes) createAttributes{};
		decltype(&::MFCreateFile) createFile{};
		decltype(&::MFCreateMediaType) createMediaType{};
		decltype(&::MFGetStrideForBitmapInfoHeader) getStride{};
		decltype(&::MFCreateSourceReaderFromByteStream) createSourceReader{};
		decltype(&::MFPCreateMediaPlayer) createPlayer{};

	private:
		struct ModuleCloser
		{
			void operator()(HMODULE module) const;
		};
		using Module = std::unique_ptr<std::remove_pointer_t<HMODULE>, ModuleCloser>;
		Module platform_;
		Module reader_;
		Module play_;
	};

	// The cache and each active reader/player hold shared ownership of the modules. No DLL is
	// unloaded while a media object or a matching MFStartup/MFShutdown pair can still use it.
	std::shared_ptr<const Api> acquire();
	std::wstring_view unavailable_message();

	namespace testing
	{
		// Suppresses new acquisitions on this thread only. Does not alter the process cache,
		// an existing player, environment variables, installed DLLs or other worker threads.
		class ScopedUnavailableBackend final
		{
		public:
			ScopedUnavailableBackend();
			~ScopedUnavailableBackend();
			ScopedUnavailableBackend(const ScopedUnavailableBackend&) = delete;
			ScopedUnavailableBackend& operator=(const ScopedUnavailableBackend&) = delete;
		private:
			bool previous_{};
		};
	}
}
