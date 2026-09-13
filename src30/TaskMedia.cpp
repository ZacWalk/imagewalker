// ImageWalker by Zac Walker
// Wires native transport, metadata, keyboard commands and deterministic close into task chrome.

#include "TaskMedia.h"

#include <algorithm>
#include <format>

namespace iw
{
	namespace
	{
		enum ControlId { fileName = 1, instructions, playButton, pauseButton, stopButton,
			seekSlider, volumeSlider, metadataLabel };

		const wchar_t* state_text(const media::PlaybackState state)
		{
			switch (state)
			{
			case media::PlaybackState::opening: return L"Opening media...";
			case media::PlaybackState::ready: return L"Ready";
			case media::PlaybackState::playing: return L"Playing";
			case media::PlaybackState::paused: return L"Paused";
			case media::PlaybackState::stopped: return L"Stopped";
			case media::PlaybackState::ended: return L"Finished";
			case media::PlaybackState::error: return L"Playback failed";
			default: return L"Closed";
			}
		}
	}

	TaskMedia::~TaskMedia()
	{
		metadataStop_.request_stop();
		if (player_) player_->close();
	}

	void TaskMedia::set_media(std::filesystem::path path)
	{
		path_ = std::move(path);
	}

	bool TaskMedia::request_close()
	{
		closing_ = true;
		metadataStop_.request_stop();
		if (player_) player_->close();
		return true;
	}

	void TaskMedia::build_controls()
	{
		const auto label = [this](const int id, std::wstring text)
		{
			ui::Control control;
			control.id = id;
			control.label = std::move(text);
			panel_.add(std::move(control));
		};
		label(fileName, path_.filename().wstring());
		label(instructions, L"Windows-native playback using installed codecs. Space: play/pause. "
			L"Left/Right: seek 5 seconds. Close returns to Items and stops playback.");
		for (const auto& [id, name] : {std::pair{playButton, L"Play"}, {pauseButton, L"Pause"},
			{stopButton, L"Stop"}})
		{
			ui::Control control;
			control.id = id;
			control.kind = ui::ControlKind::button;
			control.label = name;
			control.enabled = false;
			panel_.add(std::move(control));
		}
		ui::Control seek;
		seek.id = seekSlider;
		seek.kind = ui::ControlKind::slider;
		seek.label = L"Position";
		seek.minimum = 0;
		seek.maximum = 1000;
		seek.enabled = false;
		seek.changed = [this]
		{
			if (player_) player_->seek(media::seek_position(panel_.find(seekSlider)->value, playback_.duration));
		};
		panel_.add(std::move(seek));
		ui::Control volume;
		volume.id = volumeSlider;
		volume.kind = ui::ControlKind::slider;
		volume.label = L"Volume";
		volume.minimum = 0;
		volume.maximum = volume.value = 100;
		volume.suffix = L"%";
		volume.changed = [this]
		{
			if (player_) player_->volume(static_cast<float>(panel_.find(volumeSlider)->value) / 100.0f);
		};
		panel_.add(std::move(volume));
		label(metadataLabel, L"Reading media information...");
	}

	std::vector<platform::ToolbarItem> TaskMedia::toolbar_items()
	{
		std::vector<platform::ToolbarItem> items;
		const auto weak = std::weak_ptr<TaskMedia>(std::static_pointer_cast<TaskMedia>(shared_from_this()));
		for (const auto& [id, name] : {std::pair{playButton, L"Play"}, {pauseButton, L"Pause"},
			{stopButton, L"Stop"}})
		{
			auto command = std::make_shared<platform::Command>();
			command->name = command->tooltip = name;
			command->toolbarText = [name] { return std::wstring(name); };
			command->enabled = [weak, id]
			{
				const auto self = weak.lock();
				const auto* control = self ? self->panel_.find(id) : nullptr;
				return self && !self->closing_ && control && control->enabled;
			};
			command->invoke = [weak, id]
			{
				if (const auto self = weak.lock())
					if (auto* control = self->panel_.find(id)) self->control_activated(*control);
			};
			items.push_back(platform::ToolbarItem::action(std::move(command)));
		}
		items.push_back(platform::ToolbarItem::separator());
		append_window_commands(items);
		return items;
	}

	void TaskMedia::start_playback()
	{
		if (initialized_ || closing_) return;
		initialized_ = true;
		const auto weak = std::weak_ptr<TaskMedia>(std::static_pointer_cast<TaskMedia>(shared_from_this()));
		player_ = media::create_player(frame_, [weak](const media::PlaybackStatus& status)
		{
			if (const auto self = weak.lock(); self && !self->closing_) self->update_playback(status);
		});
		if (player_)
		{
			player_->bounds(contentBounds_);
			player_->open(path_);
		}
		else
		{
			playback_.state = media::PlaybackState::error;
			playback_.error = L"Windows could not create the media playback surface.";
			update_playback(playback_);
		}
		const auto stop = metadataStop_.get_token();
		platform::queue_work(platform::WorkQueue::metadata, [weak, path = path_, stop]
		{
			media::Info info;
			try { info = media::probe(path, stop); }
			catch (...) { info.error = L"Unable to read media information."; }
			if (stop.stop_requested()) return;
			platform::queue_ui([weak, info = std::move(info), stop]
			{
				const auto self = weak.lock();
				if (!self || self->closing_ || stop.stop_requested()) return;
				std::wstring text = info.error;
				if (info.hasDuration) text += L"\nDuration: " + media::duration_text(info.duration);
				if (info.width > 0 && info.height > 0)
					text += std::format(L"\nVideo: {} x {} pixels", info.width, info.height);
				if (info.frameRate > 0) text += std::format(L"\nFrame rate: {:.3f} fps", info.frameRate);
				if (!info.codec.empty()) text += L"\n" + info.codec;
				if (text.empty()) text = L"No media information is available.";
				if (auto* control = self->panel_.find(metadataLabel)) control->label = std::move(text);
				self->invalidate();
			});
		});
	}

	void TaskMedia::layout_hosted_controls()
	{
		start_playback();
		if (player_) player_->bounds(contentBounds_);
	}

	void TaskMedia::draw_content(const ui::CanvasRenderer& renderer, const recti bounds)
	{
		if (!player_)
			renderer.text(playback_.error.empty() ? L"Opening media..." : playback_.error, bounds,
				platform::system_color(platform::SystemColor::windowText),
				platform::TextFormat::left);
	}

	void TaskMedia::update_playback(const media::PlaybackStatus& status)
	{
		const auto previous = playback_.state;
		playback_ = status;
		panel_.set_enabled(playButton, media::can_play(status.state));
		panel_.set_enabled(pauseButton, status.state == media::PlaybackState::playing);
		panel_.set_enabled(stopButton, status.state == media::PlaybackState::playing ||
			status.state == media::PlaybackState::paused || status.state == media::PlaybackState::opening);
		panel_.set_enabled(seekSlider, status.canSeek && status.hasDuration && status.duration > 0);
		if (auto* seek = panel_.find(seekSlider))
		{
			const auto label = L"Position: " + media::duration_text(status.position) + L" / " +
				(status.hasDuration ? media::duration_text(status.duration) : L"unknown");
			if (seek->label != label) { seek->label = label; invalidate(); }
			if (!panel_.dragging() && status.duration > 0)
				panel_.set_value(seekSlider, static_cast<int>(std::clamp(
					static_cast<double>(status.position) / status.duration, 0.0, 1.0) * 1000));
		}
		const std::wstring text = status.state == media::PlaybackState::error
			? status.error : std::wstring(state_text(status.state)) + L" - " + path_.filename().wstring();
		if (displayedStatus_ != text)
		{
			displayedStatus_ = text;
			set_status(text);
		}
		if (previous != status.state) refresh_commands();
	}

	void TaskMedia::control_activated(ui::Control& control)
	{
		if (!player_ || !control.enabled || closing_) return;
		switch (control.id)
		{
		case playButton: player_->play(); break;
		case pauseButton: player_->pause(); break;
		case stopButton: player_->stop(); break;
		default: break;
		}
	}

	bool TaskMedia::content_key(const platform::KeyInput& input)
	{
		if (!player_ || input.control || input.alt || closing_) return false;
		if (input.key == platform::KeyCode::space)
		{
			if (playback_.state == media::PlaybackState::playing) player_->pause();
			else player_->play();
			return true;
		}
		if (input.key == platform::KeyCode::left || input.key == platform::KeyCode::right)
		{
			const auto step = 5 * media::ticksPerSecond;
			const auto position = playback_.position;
			const auto target = input.key == platform::KeyCode::left ? (std::max)(std::int64_t{0}, position - step)
				: position < playback_.duration - step ? position + step : playback_.duration;
			player_->seek(target);
			return true;
		}
		return false;
	}
}
