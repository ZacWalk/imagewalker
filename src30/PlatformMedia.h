// ImageWalker by Zac Walker
// UI-thread media transport with a separate native video composition surface.

#pragma once

#include "Platform.h"
#include "Media.h"

namespace iw::media
{
	enum class PlaybackState { closed, opening, ready, playing, paused, stopped, ended, error };

	struct PlaybackStatus
	{
		PlaybackState state{PlaybackState::closed};
		std::int64_t position{};
		std::int64_t duration{};
		float volume{1.0f};
		bool hasDuration{};
		bool canSeek{};
		bool hasVideo{};
		std::wstring error;
	};

	inline bool can_play(const PlaybackState state)
	{
		return state == PlaybackState::ready || state == PlaybackState::paused ||
			state == PlaybackState::stopped || state == PlaybackState::ended;
	}

	inline std::int64_t seek_position(const int fraction, const std::int64_t duration)
	{
		if (fraction <= 0 || duration <= 0) return 0;
		if (fraction >= 1000) return duration;
		// Split before multiplying so even malformed near-INT64_MAX durations are safe.
		return (duration / 1000) * fraction + (duration % 1000) * fraction / 1000;
	}

	class Player
	{
	public:
		virtual ~Player() = default;
		virtual void open(const std::filesystem::path& path) = 0;
		virtual void play() = 0;
		virtual void pause() = 0;
		virtual void stop() = 0;
		virtual void seek(std::int64_t ticks) = 0;
		virtual void volume(float value) = 0;
		virtual void close() = 0;
		virtual void bounds(recti bounds) = 0;
		virtual const PlaybackStatus& status() const = 0;
	};

	using StatusHandler = std::function<void(const PlaybackStatus&)>;
	std::unique_ptr<Player> create_player(const platform::WindowFramePtr& parent, StatusHandler changed);
}
