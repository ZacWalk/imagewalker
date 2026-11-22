// ImageWalker by Zac Walker
// Browse transport and presentation on the existing image canvas.

#include "ImageCanvas.h"
#include "Paths.h"
#include "PlatformMedia.h"

#include <algorithm>
#include <utility>

namespace iw
{
	namespace
	{
		constexpr auto center = platform::TextFormat::singleLine | platform::TextFormat::center |
		                        platform::TextFormat::verticalCenter | platform::TextFormat::endEllipsis;
	}

	bool ImageCanvas::media_preview() const
	{
		return !fullscreen_ && !path_.empty() && media::is_media(displayedKind_);
	}

	void ImageCanvas::play_media(const std::filesystem::path& path)
	{
		if (!frame_ || fullscreen_) return;
		if (!media_preview() || !paths::equal(path_, path)) load(path);
		if (!media_preview()) return;
		if (!player_)
		{
			player_ = media::create_player(frame_, [weak = std::weak_ptr(reactorBinding_),
			                                        this](const media::PlaybackStatus& status)
			{
				if (weak.expired()) return;
				if (status.state == media::PlaybackState::error) invalidate_image_pane();
				else if (!mediaBar_.is_empty() && frame_) frame_->invalidate(mediaBar_);
			});
			player_->bounds(mediaPicture_);
			player_->volume(mediaMuted_ ? 0.0f : static_cast<float>(mediaVolume_) / 100.0f);
			player_->open(path_);
		}
		else switch (const auto state = player_->status().state)
			{
			case media::PlaybackState::playing: player_->pause(); break;
			case media::PlaybackState::error: player_->open(path_); break;
			default:
				if (media::can_play(state)) player_->play();
				break;
			}
		invalidate_image_pane();
	}

	bool ImageCanvas::stop_media()
	{
		if (!player_) return false;
		player_.reset();
		if (std::exchange(mediaSeeking_, false) && frame_) frame_->release_capture();
		invalidate_image_pane();
		return true;
	}

	void ImageCanvas::toggle_media_mute()
	{
		mediaMuted_ = !mediaMuted_;
		if (player_) player_->volume(mediaMuted_ ? 0.0f : static_cast<float>(mediaVolume_) / 100.0f);
		if (frame_ && !mediaBar_.is_empty()) frame_->invalidate(mediaBar_);
	}

	void ImageCanvas::show_media_volume()
	{
		std::vector<platform::PopupItem> levels;
		for (const int volume : {0, 25, 50, 75, 100})
		{
			auto command = std::make_shared<platform::Command>();
			command->checked = [this, volume] { return volume == (mediaMuted_ ? 0 : mediaVolume_); };
			command->invoke = [this, volume]
			{
				mediaMuted_ = volume == 0;
				if (volume > 0) mediaVolume_ = volume;
				if (player_) player_->volume(static_cast<float>(volume) / 100.0f);
				if (frame_) frame_->invalidate(mediaBar_);
			};
			levels.push_back(platform::PopupItem::action(std::move(command),
			                                           volume ? std::to_wstring(volume) + L"%" : L"Mute"));
		}
		platform::show_popup_menu(frame_, frame_->client_to_screen({mediaMute_.x, mediaMute_.bottom()}), levels);
	}

	void ImageCanvas::layout_media_preview()
	{
		mediaPicture_ = mediaBar_ = mediaPlay_ = mediaMute_ = mediaSeek_ = mediaTime_ = {};
		if (!media_preview()) return;
		const auto view = primaryView_.bounds.intersection(imageScroll_.viewport(imagePane_));
		if (view.width < metric(94) || view.height < metric(40))
		{
			if (player_) player_->bounds({});
			return;
		}
		const auto barHeight = metric(34);
		mediaBar_ = {view.x, view.bottom() - barHeight, view.width, barHeight};
		mediaPicture_ = {view.x, view.y, view.width, (std::max)(0, mediaBar_.y - view.y - metric(6))};
		const auto buttonWidth = metric(view.width >= metric(260) ? 58 : 46);
		mediaPlay_ = {view.x, mediaBar_.y, buttonWidth, barHeight};
		mediaMute_ = {view.right() - buttonWidth, mediaBar_.y, buttonWidth, barHeight};
		const auto textWidth = view.width >= metric(284) ? metric(104) : 0;
		mediaTime_ = {mediaMute_.x - textWidth, mediaBar_.y, textWidth, barHeight};
		const auto gap = metric(10);
		mediaSeek_ = {mediaPlay_.right() + gap, mediaBar_.y, (std::max)(0, mediaTime_.x - mediaPlay_.right() - gap * 2),
		              barHeight};
		if (player_) player_->bounds(mediaPicture_);
	}

	void ImageCanvas::paint_media_preview(const ui::CanvasRenderer& renderer) const
	{
		if (mediaBar_.is_empty()) return;
		const auto text = platform::system_color(platform::SystemColor::windowText);
		const auto secondary = platform::system_color(platform::SystemColor::grayText);
		const auto face = platform::system_color(platform::SystemColor::face);
		const auto shadow = platform::system_color(platform::SystemColor::shadow);
		const auto accent = platform::system_color(platform::SystemColor::highlight);
		const auto* status = player_ ? &player_->status() : nullptr;
		const bool playing = status && status->state == media::PlaybackState::playing;
		const bool opening = status && status->state == media::PlaybackState::opening;
		const bool failed = status && status->state == media::PlaybackState::error;
		renderer.fill(mediaPicture_, face);
		if (!player_ || !player_->picture())
		{
			const auto inset = metric(14);
			const auto area = mediaPicture_.inflate(-inset);
			if (mediaHasPoster_ && !failed)
			{
				const auto& photo = initialImage_;
				if (!area.is_empty())
					renderer.image({photo.width, photo.height, photo.pixels, false, imageGeneration_},
					               ui::fit_centered({photo.width, photo.height}, area));
			}
			else if (!area.is_empty())
			{
				const auto y = area.y + area.height / 2;
				renderer.text(path_.filename().wstring(), {area.x, y - metric(20), area.width, metric(20)}, text,
				              center);
				const auto hint = failed                              ? status->error
				                  : opening                           ? std::wstring(L"Opening media...")
				                  : status && !status->notice.empty() ? status->notice
				                  : playing                           ? std::wstring(L"Playing")
				                                                      : std::wstring(L"Press Space to play");
				renderer.text(hint, {area.x, y, area.width, metric(20)}, failed ? text : secondary, center);
			}
		}
		renderer.fill(mediaBar_, face);
		renderer.outline(mediaBar_, shadow);
		renderer.fill(mediaPlay_.inflate(-metric(3)), platform::calc_hande_color(false, false, false));
		renderer.text(playing ? L"Pause" : L"Play", mediaPlay_, opening ? secondary : text, center);
		renderer.text(mediaMuted_ ? L"Muted" : L"Sound", mediaMute_, text, center);
		if (mediaSeek_.width <= 0) return;
		const auto y = mediaSeek_.y + (mediaSeek_.height - metric(4)) / 2;
		const auto metadata = metadataCache_.find(path_);
		const auto duration = status && status->hasDuration ? status->duration
		                      : metadata != metadataCache_.end() && metadata->second.hasDuration
		                          ? metadata->second.duration
		                          : 0;
		const auto position = duration <= 0   ? 0
		                      : mediaSeeking_ ? media::seek_position(mediaSeekFraction_, duration)
		                      : status        ? status->position
		                                      : 0;
		const auto ratio = duration > 0 ? std::clamp(static_cast<double>(position) / duration, 0.0, 1.0) : 0.0;
		const auto progress = static_cast<int>(mediaSeek_.width * ratio);
		renderer.fill({mediaSeek_.x, y, mediaSeek_.width, metric(4)}, shadow);
		if (status && status->canSeek)
		{
			renderer.fill({mediaSeek_.x, y, progress, metric(4)}, accent);
			const auto thumb = metric(4);
			renderer.fill({mediaSeek_.x + progress - thumb / 2, y - metric(3), thumb, metric(10)}, accent);
		}
		if (!mediaTime_.is_empty())
		{
			const auto time = duration > 0 ? media::duration_text(position) + L" / " + media::duration_text(duration)
			                               : media::duration_text(position);
			renderer.text(time, mediaTime_, text, center);
		}
	}

	bool ImageCanvas::media_key(const platform::KeyInput& input)
	{
		if (!media_preview() || input.control || input.alt || input.shift) return false;
		if (input.key == platform::KeyCode::f7)
		{
			toggle_media_mute();
			return true;
		}
		if (input.key != platform::KeyCode::space && input.key != platform::KeyCode::enter) return false;
		const auto focused = active_selection().focus();
		if (focused >= 0 && static_cast<size_t>(focused) < files_.size() && !paths::equal(files_[focused], path_))
			return false;
		play_media(path_);
		return true;
	}

	bool ImageCanvas::media_mouse(const InputEvent event, const platform::MouseInput& input)
	{
		if (!media_preview()) return false;
		if (mediaSeeking_)
		{
			if (event == InputEvent::captureLost)
			{
				mediaSeeking_ = false;
				invalidate_image_pane();
				return true;
			}
			if (event == InputEvent::move || event == InputEvent::leftButtonUp)
			{
				if (mediaSeek_.width > 0)
					mediaSeekFraction_ = static_cast<int>(
						1000LL * std::clamp(input.point.x - mediaSeek_.x, 0, mediaSeek_.width) / mediaSeek_.width);
				if (event == InputEvent::leftButtonUp)
				{
					mediaSeeking_ = false;
					if (frame_) frame_->release_capture();
					if (player_) player_->seek(media::seek_position(mediaSeekFraction_, player_->status().duration));
				}
				invalidate_image_pane();
				return true;
			}
			return false;
		}
		if (event != InputEvent::leftButtonDown || !mediaBar_.contains(input.point)) return false;
		if (frame_) frame_->set_focus();
		if (mediaPlay_.contains(input.point)) play_media(path_);
		else if (mediaMute_.contains(input.point)) show_media_volume();
		else if (mediaSeek_.contains(input.point) && player_ && player_->status().canSeek)
		{
			mediaSeeking_ = true;
			mediaSeekFraction_ = static_cast<int>(1000LL * (input.point.x - mediaSeek_.x) / mediaSeek_.width);
			if (frame_) frame_->set_capture();
			invalidate_image_pane();
		}
		return true;
	}
}
