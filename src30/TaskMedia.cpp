// ImageWalker by Zac Walker
// Wires the player's transport, metadata, keyboard commands and deterministic close into task chrome.

#include "TaskMedia.h"

#include "Paths.h"
#include "TaskEdit.h"
#include "Undo.h"

#include <algorithm>
#include <cwctype>
#include <format>

namespace iw
{
	namespace
	{
		enum ControlId
		{
			fileName = 1,
			instructions,
			playButton,
			pauseButton,
			stopButton,
			seekSlider,
			volumeSlider,
			metadataLabel,
			trimHeading,
			trimStart,
			trimEnd,
			resetTrim,
			turnHeading,
			turnLeft,
			turnRight,
			resetTurn,
			removeSound,
			saveHeading,
			indexFirst
		};

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
		saveStop_.request_stop();
		if (player_) player_->close();
	}

	void TaskMedia::set_media(std::filesystem::path path, const bool edit)
	{
		path_ = std::move(path);
		editing_ = edit;
	}

	bool TaskMedia::request_close()
	{
		if (saving_) return false;
		if (!closing_ && editing_ && draft_.changed())
		{
			platform::ChoiceDefinition choice;
			choice.title = title();
			choice.heading = L"Save the media edits before leaving?";
			choice.message = path_.filename().wstring();
			choice.buttons = {{1, L"Save"}, {2, L"Don't Save"}, {3, L"Cancel"}};
			choice.defaultButton = 1;
			switch (platform::show_choice(frame_, choice))
			{
			case 1: save_edit(false, true); return false;
			case 2: break;
			default: return false;
			}
		}
		closing_ = true;
		metadataStop_.request_stop();
		if (player_) player_->close();
		return true;
	}

	void TaskMedia::build_controls()
	{
		const auto label = [this](const int id, std::wstring text, const auto kind)
		{
			ui::Control control;
			control.id = id;
			control.kind = kind;
			control.label = std::move(text);
			panel_.add(std::move(control));
		};
		using Kind = ui::ControlKind;
		panel_.set_compact(editing_);
		label(fileName, path_.filename().wstring(), Kind::heading);
		if (editing_)
		{
			label(instructions, L"Trim and edit without re-encoding. Save keeps an original backup.", Kind::label);
			label(trimHeading, L"Trim", Kind::heading);
			label(metadataLabel, L"Checking the media tracks...", Kind::label);
			label(trimStart, L"Set in (I)", Kind::button);
			label(trimEnd, L"Set out (O)", Kind::button);
			label(resetTrim, L"Clear trim", Kind::button);
			label(turnHeading, L"Video", Kind::heading);
			label(removeSound, L"Remove sound", Kind::checkBox);
			label(turnLeft, L"\u21b6 Left", Kind::button);
			label(turnRight, L"\u21b7 Right", Kind::button);
			label(resetTurn, L"Reset turn", Kind::button);
			label(saveHeading, L"Save options", Kind::heading);
			label(indexFirst, L"Put the MP4/QuickTime index first", Kind::checkBox);
			panel_.set_checked(indexFirst, true);
			panel_.find(removeSound)->changed = [this]
			{
				auto next = draft_;
				next.silent = panel_.find(removeSound)->checked;
				change_draft(next);
			};
			for (const int id : {trimStart, trimEnd, resetTrim}) panel_.find(id)->buttonRow = 1;
			for (const int id : {turnLeft, turnRight, resetTurn}) panel_.find(id)->buttonRow = 2;
			inspect_media();
		}
		else
		{
			label(instructions, L"Space: play/pause. F7: mute/unmute. Left/Right: seek 5 seconds.", Kind::label);
			for (const auto& [id, name] :
			     {std::pair{playButton, L"Play"}, {pauseButton, L"Pause"}, {stopButton, L"Stop"}})
			{
				label(id, name, Kind::button);
				panel_.set_enabled(id, false);
			}
			ui::Control seek;
			seek.id = seekSlider;
			seek.kind = Kind::slider;
			seek.label = L"Position";
			seek.maximum = 1000;
			seek.showValue = false;
			seek.enabled = false;
			seek.changed = [this]
			{
				this->seek(media::seek_position(panel_.find(seekSlider)->value, playback_.duration));
			};
			panel_.add(std::move(seek));
			label(metadataLabel, L"Reading media information...", Kind::label);
		}
		ui::Control volume;
		volume.id = volumeSlider;
		volume.kind = ui::ControlKind::slider;
		volume.label = L"Volume";
		volume.minimum = 0;
		volume.maximum = volume.value = 100;
		volume.suffix = L"%";
		volume.changed = [this]
		{
			const int value = panel_.find(volumeSlider)->value;
			if (value > 0) unmutedVolume_ = value;
			apply_volume();
		};
		panel_.add(std::move(volume));
		if (editing_) update_edit();
	}

	std::vector<platform::ToolbarItem> TaskMedia::toolbar_items()
	{
		std::vector<platform::ToolbarItem> items;
		const auto weak = std::weak_ptr<TaskMedia>(std::static_pointer_cast<TaskMedia>(shared_from_this()));
		if (editing_)
		{
			const auto add = [&](const wchar_t* name, auto run, auto enabled)
			{
				auto command = std::make_shared<platform::Command>();
				command->name = command->tooltip = name;
				command->toolbarText = [name]
				{
					return std::wstring(name);
				};
				command->enabled = [weak, enabled]
				{
					const auto self = weak.lock();
					return self && enabled(*self);
				};
				command->invoke = [weak, run]
				{
					if (const auto self = weak.lock()) run(*self);
				};
				items.push_back(platform::ToolbarItem::action(std::move(command)));
			};
			add(L"Play / Pause", [](TaskMedia& self) { self.toggle_play(); }, [](const TaskMedia& self)
			{
				return !self.saving_ &&
				       (media::can_play(self.playback_.state) || self.playback_.state == media::PlaybackState::playing);
			});
			add(L"Undo", [](TaskMedia& self) { self.undo_draft(); },
			    [](const TaskMedia& self) { return self.can_undo_draft(); });
			add(L"Save", [](TaskMedia& self) { self.save_edit(false); },
			    [](const TaskMedia& self) { return self.can_copy() && self.draft_.changed(); });
			add(L"Save As", [](TaskMedia& self) { self.save_edit(true); },
			    [](const TaskMedia& self) { return self.can_copy(); });
			add(L"Cancel save", [](TaskMedia& self)
			{
				self.saveStop_.request_stop();
				self.set_status(L"Cancelling save...");
			}, [](const TaskMedia& self) { return self.saving_ && !self.saveStop_.stop_requested(); });
			items.push_back(platform::ToolbarItem::separator());
			append_window_commands(items);
			return items;
		}
		for (const auto& [id, name] : {std::pair{playButton, L"Play"}, {pauseButton, L"Pause"}, {stopButton, L"Stop"}})
		{
			auto command = std::make_shared<platform::Command>();
			command->name = command->tooltip = name;
			command->toolbarText = [name]
			{
				return std::wstring(name);
			};
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
			player_->bounds(video_bounds(contentBounds_));
			player_->open(path_, !editing_);
		}
		else
		{
			playback_.state = media::PlaybackState::error;
			playback_.error = L"Windows could not create the media playback surface.";
			update_playback(playback_);
		}
		if (editing_) return;
		const auto stop = metadataStop_.get_token();
		platform::queue_work(platform::WorkQueue::metadata, [weak, path = path_, stop]
		{
			media::Info info;
			try
			{
				info = media::probe(path, stop);
			}
			catch (...)
			{
				info.error = L"Unable to read media information.";
			}
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
		if (player_) player_->bounds(video_bounds(contentBounds_));
	}

	void TaskMedia::draw_content(const ui::CanvasRenderer& renderer, const app::recti bounds)
	{
		// The player draws the video over this once its first picture is decoded.
		renderer.fill(video_bounds(bounds), colors::black);
		if (editing_) draw_timeline(renderer, bounds);
		const auto text = playback_.state == media::PlaybackState::error ? playback_.error
		                  : !player_ || playback_.state == media::PlaybackState::opening
		                      ? std::wstring(L"Opening media...")
		                  : playback_.hasVideo ? std::wstring{}
		                                       : path_.filename().wstring();
		if (text.empty()) return;
		const int inset = metric(24);
		const auto picture = video_bounds(bounds);
		renderer.text(text,
		              {picture.x + inset, picture.y + inset, (std::max)(0, picture.width - inset * 2),
		               (std::max)(0, picture.height - inset * 2)},
		              {230, 230, 230},
		              platform::TextFormat::center | platform::TextFormat::verticalCenter |
		                  platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
	}

	void TaskMedia::update_playback(const media::PlaybackStatus& status)
	{
		const auto previous = playback_.state;
		playback_ = status;
		if (editing_)
		{
			if (drag_ == Drag::none && status.state != media::PlaybackState::error && status.hasDuration)
				position_ = status.position;
			if (drag_ == Drag::none && draft_.last && position_ >= draft_.last &&
			    status.state == media::PlaybackState::playing && player_)
				player_->pause();
			if (previous != status.state && status.state == media::PlaybackState::error && !saving_)
				set_status(status.error);
			if (previous != status.state) refresh_commands();
			invalidate();
			return;
		}
		panel_.set_enabled(playButton, media::can_play(status.state));
		panel_.set_enabled(pauseButton, status.state == media::PlaybackState::playing);
		panel_.set_enabled(stopButton, status.state == media::PlaybackState::playing ||
		                                   status.state == media::PlaybackState::paused ||
		                                   status.state == media::PlaybackState::opening);
		panel_.set_enabled(seekSlider, status.canSeek && status.hasDuration && status.duration > 0);
		if (auto* seek = panel_.find(seekSlider))
		{
			const auto label = L"Position: " + media::duration_text(status.position) + L" / " +
			                   (status.hasDuration ? media::duration_text(status.duration) : L"unknown");
			if (seek->label != label)
			{
				seek->label = label;
				invalidate();
			}
			if (!panel_.dragging() && status.duration > 0)
				panel_.set_value(seekSlider,
				                 static_cast<int>(
									 std::clamp(static_cast<double>(status.position) / status.duration, 0.0, 1.0) *
									 1000));
		}
		const std::wstring text = status.state == media::PlaybackState::error
		                              ? status.error
		                              : std::wstring(state_text(status.state)) + L" - " + path_.filename().wstring() +
		                                    (status.notice.empty() ? L"" : L" - " + status.notice);
		if (displayedStatus_ != text)
		{
			displayedStatus_ = text;
			set_status(text);
		}
		if (previous != status.state) refresh_commands();
	}

	void TaskMedia::control_activated(ui::Control& control)
	{
		if (!control.enabled || closing_ || saving_) return;
		if (editing_)
		{
			auto next = draft_;
			switch (control.id)
			{
			case trimStart: set_cut(true, position_); return;
			case trimEnd: set_cut(false, position_); return;
			case resetTrim: next.first = next.last = 0; break;
			case turnLeft: next.turn = (next.turn + 3) % 4; break;
			case turnRight: next.turn = (next.turn + 1) % 4; break;
			case resetTurn: next.turn = 0; break;
			default: return;
			}
			change_draft(next);
			return;
		}
		if (!player_) return;
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
		if (closing_) return false;
		if (editing_)
		{
			const auto key = std::towupper(input.character);
			if (input.key == platform::KeyCode::escape && content_dragging())
			{
				finish_drag(true);
				return true;
			}
			if (input.control && key == L'Z') return undo_draft();
			if ((input.alt && input.key == platform::KeyCode::enter) || (input.control && key == L'S'))
			{
				save_edit(input.shift);
				return true;
			}
			if (!input.control && !input.alt && !input.shift && (key == L'I' || key == L'O') && can_copy())
			{
				set_cut(key == L'I', position_);
				return true;
			}
		}
		if (!player_ || input.control || input.alt || input.shift || saving_) return false;
		if (input.key == platform::KeyCode::f7)
		{
			if (const auto* volume = panel_.find(volumeSlider))
			{
				const int value = volume->value > 0 ? 0 : unmutedVolume_;
				panel_.set_value(volumeSlider, value);
				apply_volume();
			}
			return true;
		}
		if (input.key == platform::KeyCode::space)
		{
			toggle_play();
			return true;
		}
		if (input.key == platform::KeyCode::left || input.key == platform::KeyCode::right)
		{
			const auto step = 5 * media::ticksPerSecond;
			const auto position = editing_ ? position_ : playback_.position;
			const auto duration = editing_ ? editInfo_.duration : playback_.duration;
			const auto target = input.key == platform::KeyCode::left ? (std::max)(std::int64_t{0}, position - step)
			                    : position < duration - step         ? position + step
			                                                         : duration;
			seek(target);
			return true;
		}
		return false;
	}

	void TaskMedia::inspect_media()
	{
		metadataStop_.request_stop();
		metadataStop_ = {};
		inspecting_ = true;
		source_.reset();
		digest_.reset();
		editInfo_ = {};
		const auto stop = metadataStop_.get_token();
		const auto weak = weak_from_this();
		if (!platform::queue_work(platform::WorkQueue::metadata,
		                          [weak, path = path_, stop, job = metadataStop_]() mutable
		{
			const std::stop_callback exit(platform::exit_token(), [&] { job.request_stop(); });
			media::edit::Info info;
			std::optional<files::FileSnapshot> source;
			std::optional<std::array<unsigned char, 32>> digest;
			try
			{
				source = files::snapshot_file(path);
				if (source && source->exists && !source->directory)
				{
					info = media::edit::inspect(path, paths::lowercase_extension(path), stop);
					if (info.error.empty()) digest = files::file_digest(path, stop);
					if (info.error.empty() && (!digest || !files::matches_snapshot(path, *source)))
						info.error = L"The input changed or its contents could not be verified.";
				}
				else info.error = L"The media file is not available.";
			}
			catch (const std::exception&)
			{
				info.error = L"Could not inspect the media file.";
			}
			if (stop.stop_requested()) return;
			platform::queue_ui([weak, source, digest, info = std::move(info), stop]
			{
				const auto self = std::static_pointer_cast<TaskMedia>(weak.lock());
				if (!self || stop.stop_requested() || self->closing_) return;
				self->source_ = source;
				self->digest_ = digest;
				self->editInfo_ = info;
				self->inspecting_ = false;
				self->update_edit();
			});
		}))
		{
			inspecting_ = false;
			editInfo_.error = L"The media analysis worker is unavailable.";
		}
	}

	bool TaskMedia::can_copy() const
	{
		return editing_ && !saving_ && !inspecting_ && !closing_ && source_ && digest_ && editInfo_.error.empty();
	}

	void TaskMedia::set_edit_status()
	{
		if (saving_) return;
		if (inspecting_) set_status(L"Checking media streams and verifying the original...");
		else if (!editInfo_.error.empty()) set_status(editInfo_.error);
		else
		{
			auto status = path_.filename().wstring();
			if (draft_.first || draft_.last)
			{
				const auto end = draft_.last ? draft_.last : editInfo_.duration;
				status += std::format(L"  |  {} - {} ({} kept)", media::duration_text(draft_.first),
				                      media::duration_text(end), media::duration_text(end - draft_.first));
			}
			if (draft_.silent) status += L"  |  No sound";
			if (draft_.turn) status += std::format(L"  |  Turn {} degrees", draft_.turn * 90);
			set_status(std::move(status));
		}
	}

	void TaskMedia::update_edit()
	{
		const bool ready = can_copy();
		const bool trim = ready && editInfo_.duration > 0;
		panel_.set_checked(removeSound, draft_.silent);
		for (const int id : {trimStart, trimEnd}) panel_.set_enabled(id, trim);
		panel_.set_enabled(resetTrim, trim && (draft_.first || draft_.last));
		panel_.set_enabled(removeSound, ready && editInfo_.video && editInfo_.audio);
		panel_.set_enabled(turnLeft, ready && editInfo_.turn);
		panel_.set_enabled(turnRight, ready && editInfo_.turn);
		panel_.set_enabled(resetTurn, ready && draft_.turn != 0);
		panel_.set_enabled(indexFirst, ready && (editInfo_.mp4 || editInfo_.turn));
		const bool video = inspecting_ || editInfo_.video;
		for (const int id : {turnHeading, turnLeft, turnRight, resetTurn, removeSound})
			panel_.find(id)->visible = video;
		if (auto* note = panel_.find(metadataLabel))
		{
			note->label = inspecting_ ? L"Checking the media tracks..." : editInfo_.error;
			if (!inspecting_ && note->label.empty())
				note->label =
					editInfo_.duration > 0
						? L"Drag the end handles, or mark the playhead with I and O. The copy may start at an earlier keyframe."
						: L"This file has no known duration. It can be copied, but not trimmed.";
		}
		set_edit_status();
		refresh_commands();
		invalidate();
	}

	void TaskMedia::apply_volume()
	{
		if (const auto* control = panel_.find(volumeSlider); player_ && control)
			player_->volume(draft_.silent ? 0.0f : static_cast<float>(control->value) / 100.0f);
	}

	void TaskMedia::push_undo(const media::edit::Draft& previous)
	{
		if (previous == draft_) return;
		if (undo_.size() == 64) undo_.erase(undo_.begin());
		undo_.push_back(previous);
	}

	void TaskMedia::change_draft(const media::edit::Draft draft)
	{
		const auto previous = std::exchange(draft_, draft);
		push_undo(previous);
		if (player_ && previous.turn != draft.turn) player_->turn(draft.turn);
		if (previous.silent != draft.silent) apply_volume();
		update_edit();
	}

	bool TaskMedia::undo_draft()
	{
		if (!can_undo_draft()) return false;
		if (content_dragging())
		{
			finish_drag(true);
			return true;
		}
		const auto previous = draft_;
		draft_ = undo_.back();
		undo_.pop_back();
		if (player_ && previous.turn != draft_.turn) player_->turn(draft_.turn);
		if (previous.silent != draft_.silent) apply_volume();
		update_edit();
		return true;
	}

	void TaskMedia::set_cut(const bool first, const std::int64_t where)
	{
		if (editInfo_.duration <= 0) return;
		auto value = draft_;
		const auto step = (std::min)(editInfo_.duration, media::ticksPerSecond / 100);
		if (first)
			value.first = std::clamp(where, std::int64_t{0}, (value.last ? value.last : editInfo_.duration) - step);
		else
		{
			const auto end = std::clamp(where, value.first + step, editInfo_.duration);
			value.last = end == editInfo_.duration ? 0 : end;
		}
		if (content_dragging())
		{
			draft_ = value;
			set_edit_status();
		}
		else change_draft(value);
	}

	app::recti TaskMedia::video_bounds(app::recti bounds) const
	{
		if (editing_) bounds.height = (std::max)(0, bounds.height - metric(64));
		return bounds;
	}

	std::int64_t TaskMedia::timeline_position() const
	{
		return content_dragging() ? dragPosition_ : position_;
	}

	void TaskMedia::draw_timeline(const ui::CanvasRenderer& renderer, const app::recti bounds)
	{
		const auto text = platform::system_color(platform::SystemColor::windowText);
		const auto accent = platform::system_color(platform::SystemColor::highlight);
		const auto shadow = platform::system_color(platform::SystemColor::shadow);
		timeline_ = {bounds.x, video_bounds(bounds).bottom(), bounds.width, metric(64)};
		renderer.fill(timeline_, platform::system_color(platform::SystemColor::face));
		scrubber_ = {timeline_.x + metric(16), timeline_.y + metric(20), (std::max)(0, timeline_.width - metric(32)),
		             metric(8)};
		const auto duration = editInfo_.duration;
		if (duration <= 0 || scrubber_.width <= 0) return;
		const auto x = [&](const std::int64_t when)
		{
			return scrubber_.x +
			       static_cast<int>(std::clamp(static_cast<double>(when) / duration, 0.0, 1.0) * scrubber_.width);
		};
		const int in = x(draft_.first), out = x(draft_.last ? draft_.last : duration), now = x(timeline_position());
		renderer.fill(scrubber_, shadow);
		renderer.fill({in, scrubber_.y, out - in, scrubber_.height}, accent);
		const int handle = metric(7);
		renderer.fill({in - handle, scrubber_.y - metric(5), handle, metric(18)}, accent);
		renderer.fill({out, scrubber_.y - metric(5), handle, metric(18)}, accent);
		renderer.fill({now - metric(1), scrubber_.y - metric(7), metric(2), metric(22)}, text);
		renderer.text(media::duration_text(timeline_position()) + L" / " + media::duration_text(duration),
		              {scrubber_.x, timeline_.y + metric(34), scrubber_.width, metric(24)}, text,
		              platform::TextFormat::left | platform::TextFormat::singleLine);
	}

	void TaskMedia::seek(const std::int64_t where)
	{
		if (editing_) position_ = std::clamp(where, std::int64_t{}, editInfo_.duration);
		if (player_) player_->seek((std::max)(std::int64_t{}, where));
		invalidate();
	}

	void TaskMedia::finish_drag(const bool cancel)
	{
		if (!content_dragging()) return;
		const auto how = std::exchange(drag_, Drag::none);
		if (cancel) draft_ = beforeDrag_;
		else push_undo(beforeDrag_);
		if (frame_) frame_->release_capture();
		seek(cancel || how != Drag::playhead ? position_ : dragPosition_);
		if (resumeAfterDrag_ && player_)
		{
			if (position_ < draft_.first || (draft_.last && position_ >= draft_.last)) seek(draft_.first);
			player_->play();
		}
		update_edit();
	}

	bool TaskMedia::content_mouse(const platform::MouseMessage message, const platform::MouseInput& input)
	{
		using Mouse = platform::MouseMessage;
		if (!editing_ || saving_ || editInfo_.duration <= 0 || scrubber_.width <= 0) return false;
		if (message == Mouse::leave)
		{
			finish_drag(true);
			return true;
		}
		const int y = input.point.y;
		const int x = input.point.x;
		if (message == Mouse::leftButtonDown && can_copy() && timeline_.contains(input.point) &&
		    y >= scrubber_.y - metric(9) && y <= scrubber_.bottom() + metric(9))
		{
			const auto marker = [&](const std::int64_t when)
			{
				return scrubber_.x + static_cast<int>(static_cast<double>(when) / editInfo_.duration * scrubber_.width);
			};
			const auto start = marker(draft_.first), end = marker(draft_.last ? draft_.last : editInfo_.duration);
			drag_ = x >= start - metric(8) && x <= start + metric(3) ? Drag::first
			        : x >= end - metric(3) && x <= end + metric(8)   ? Drag::last
			                                                         : Drag::playhead;
			beforeDrag_ = draft_;
			resumeAfterDrag_ = playback_.state == media::PlaybackState::playing;
			if (player_) player_->pause();
			if (frame_) frame_->set_capture();
		}
		if (!content_dragging()) return false;
		if (message == Mouse::move || message == Mouse::leftButtonDown || message == Mouse::leftButtonUp)
		{
			const auto fraction = std::clamp(static_cast<double>(x - scrubber_.x) / scrubber_.width, 0.0, 1.0);
			const auto duration = editInfo_.duration;
			dragPosition_ = fraction >= 1 ? duration : static_cast<std::int64_t>(fraction * duration);
			if (drag_ == Drag::first || drag_ == Drag::last) set_cut(drag_ == Drag::first, dragPosition_);
			if (player_) player_->seek(dragPosition_);
			if (message == Mouse::leftButtonUp) finish_drag(false);
			invalidate();
			return true;
		}
		return false;
	}

	void TaskMedia::toggle_play()
	{
		if (!player_ || saving_) return;
		if (playback_.state == media::PlaybackState::playing) player_->pause();
		else
		{
			if (editing_ && (position_ < draft_.first || (draft_.last && position_ >= draft_.last))) seek(draft_.first);
			player_->play();
		}
	}

	bool TaskMedia::save_edit(const bool saveAs, const bool closeAfter)
	{
		if (!can_copy()) return false;
		finish_drag(false);
		const auto extension = paths::lowercase_extension(path_);
		const bool mov = paths::iequals(extension, L".mov") || paths::iequals(extension, L".mp4") ||
		                 paths::iequals(extension, L".m4v") || paths::iequals(extension, L".3gp") ||
		                 paths::iequals(extension, L".3g2");
		const bool same = editInfo_.sameFormat && (!draft_.turn || mov);
		if (!saveAs && same && !source_->readOnly && !source_->reparse) return start_save(path_, true, closeAfter);

		auto base = path_;
		if (!same) base.replace_extension(L".mp4");
		const auto proposed = proposed_edit_path(base);
		if (proposed.empty())
		{
			platform::show_error(L"No free name could be proposed for the copy.", title(), frame_);
			return false;
		}
		auto filter = L"*" + base.extension().wstring();
		if (same && editInfo_.mp4 && !paths::iequals(base.extension().wstring(), L".mp4")) filter += L";*.mp4";
		const auto destination = platform::choose_save_file(frame_,
		                                                    {L"Media file", filter, base.extension().wstring(),
		                                                     proposed.filename().wstring(), proposed.parent_path()});
		if (!destination) return false;
		const auto chosen = paths::lowercase_extension(*destination);
		if (!(same && paths::iequals(chosen, extension)) && !(chosen == L".mp4" && editInfo_.mp4))
		{
			platform::show_error(L"Choose the offered media format. No file was written.", title(), frame_);
			return false;
		}
		return start_save(*destination, false, closeAfter);
	}

	bool TaskMedia::start_save(const std::filesystem::path& destination, const bool inPlace, const bool closeAfter)
	{
		const auto expected = files::snapshot_file(destination);
		if (!expected || (inPlace ? expected != source_ : expected->exists))
		{
			platform::show_error(inPlace ? L"The original changed. Nothing was saved."
			                             : L"Save As needs a new, available name. Nothing was saved.",
			                     title(), frame_);
			return false;
		}
		if (player_) player_->close(); // Release the reader before an in-place publication.
		saving_ = true;
		closeAfterSave_ = closeAfter;
		saveStop_ = {};
		panel_.set_interactive(false);
		const auto stop = saveStop_.get_token();
		const auto weak = weak_from_this();
		const auto source = *source_;
		const auto digest = *digest_;
		const auto index = panel_.find(indexFirst)->checked;
		set_status(std::format(L"Copying media to {}...", destination.filename().wstring()));
		refresh_commands();
		if (platform::queue_work(platform::WorkQueue::task,
		                         [weak, destination, expected = *expected, inPlace, path = path_, source, digest,
		                          draft = draft_, index, stop, job = saveStop_]() mutable
		{
			const std::stop_callback exit(platform::exit_token(), [&] { job.request_stop(); });
			media::edit::Result result;
			std::wstring warning;
			try
			{
				const auto matches = [&]
				{
					return !stop.stop_requested() && files::matches_snapshot(path, source) &&
					       files::file_digest(path, stop) == digest;
				};
				if (!matches()) result.error = L"The input changed or the operation was cancelled. Nothing was saved.";
				else
				{
					auto journal = undo::history().begin(L"Media Edit");
					std::filesystem::path backup;
					if (journal->protect({destination}, result.error))
					{
						const auto failure = files::write_staged_file(destination, expected,
						                                              [&](const files::StagedStream& stream)
						{
							result = media::edit::copy(path, paths::lowercase_extension(destination), draft, index,
							                           stream, stop);
							return static_cast<bool>(result);
						}, [&]
						{
							if (!matches()) result.error = L"The input changed or the operation was cancelled.";
							return result && journal->validate_before({destination}, result.error) &&
							       !stop.stop_requested();
						}, &backup);
						if (failure && result.error.empty())
							result.error = L"Could not publish the copy. No file was replaced.";
						if (failure && !backup.empty()) result.error += L" Original retained at " + backup.wstring();
						if (!failure && !journal->completed({destination}, warning))
							warning = L"Saved, but Undo recording failed. " + warning;
					}
					std::wstring finishError;
					if (!journal->finish(finishError)) warning += L" " + finishError;
				}
			}
			catch (const std::exception&)
			{
				result.error = L"Media editing failed. Check whether a copy appeared before trying again.";
			}
			platform::queue_ui([weak, destination, inPlace, result = std::move(result), warning = std::move(warning)]
			{
				const auto self = std::static_pointer_cast<TaskMedia>(weak.lock());
				if (!self || self->closing_) return;
				self->saving_ = false;
				const bool close = std::exchange(self->closeAfterSave_, false);
				self->panel_.set_interactive(true);
				if (!warning.empty()) platform::show_error(warning, self->title(), self->frame_);
				if (!result)
				{
					self->update_edit();
					if (self->player_) self->player_->open(self->path_, false);
					platform::show_error(result.error + L" The draft was kept.", self->title(), self->frame_);
					return;
				}
				if (result.actualFirst != self->draft_.first && result.actualFirst >= 0)
					platform::write_diagnostic(L"Media copy starts at " + media::duration_text(result.actualFirst) +
					                           L".");
				self->draft_ = {};
				self->undo_.clear();
				const auto saved = self->host_.saved;
				if (saved) saved(destination, !inPlace && !close);
				if (close)
				{
					self->ask_host_to_close();
					return;
				}
				if (inPlace)
				{
					self->inspect_media();
					self->update_edit();
					if (self->player_)
					{
						self->player_->turn(0);
						self->player_->open(self->path_, false);
						self->apply_volume();
					}
				}
			});
		}))
			return true;
		saving_ = closeAfterSave_ = false;
		panel_.set_interactive(true);
		update_edit();
		if (player_) player_->open(path_, false);
		platform::show_error(L"The media worker could not start.", title(), frame_);
		return false;
	}
}
