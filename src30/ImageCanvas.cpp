// ImageWalker by Zac Walker
// Implements the retained two-pane image preview, selection summary, thumbnail browser, and background decoding.

#include "Platform.h"
#include "ImageCanvas.h"
#include "Files.h"
#include "Format.h"
#include "Paths.h"
#include "Media.h"

#include <algorithm>
#include <array>
#include <map>

namespace iw
{
	namespace
	{
		bool has_thumbnail(const files::ItemKind kind)
		{
			return kind == files::ItemKind::image || kind == files::ItemKind::video;
		}

		std::array<recti, 6> media_detail_columns(const recti bounds)
		{
			constexpr std::array weights{28, 20, 14, 12, 14, 12};
			std::array<recti, 6> columns{};
			int consumed = 0;
			int weight = 0;
			for (size_t index = 0; index < columns.size(); ++index)
			{
				weight += weights[index];
				const int right = static_cast<int>(static_cast<std::int64_t>(bounds.width) * weight / 100);
				columns[index] = {bounds.x + consumed, bounds.y, right - consumed, bounds.height};
				consumed = right;
			}
			return columns;
		}

		files::DecodedImage load_bitmap_resource(const platform::BitmapAsset resource, bool& hasAlpha)
		{
			const auto bitmap = platform::load_bitmap_resource(resource);
			files::DecodedImage result{
				bitmap.size.width, bitmap.size.height, bitmap.pixels,
				bitmap.size.width, bitmap.size.height
			};
			hasAlpha = bitmap.hasAlpha;
			return result;
		}
	}

	ImageCanvas::ImageCanvas() = default;

	bool ImageCanvas::create(const platform::WindowFramePtr& parent)
	{
		constexpr std::array resources{
			platform::BitmapAsset::fileFolder, platform::BitmapAsset::filePhoto,
			platform::BitmapAsset::fileDocument, platform::BitmapAsset::fileVideo,
			platform::BitmapAsset::fileAudio, platform::BitmapAsset::fileOther
		};
		for (size_t index = 0; index < resources.size(); ++index)
		{
			bool hasAlpha = false;
			typeIcons_[index] = load_bitmap_resource(resources[index], hasAlpha);
			typeIconAlpha_[index] = hasAlpha;
			detailTypeIcons_[index] = typeIcons_[index];
			while (detailTypeIcons_[index].width > 32 || detailTypeIcons_[index].height > 32)
				detailTypeIcons_[index] = files::downsample_bgra_2x(detailTypeIcons_[index]);
		}
		reactorBinding_ = std::shared_ptr<FrameReactor>(this, [](FrameReactor*)
		{
		});
		platform::WindowOptions options;
		options.className = "ImageWalker30.Canvas";
		options.child = true;
		options.visible = true;
		options.eraseBackground = false;
		options.tabStop = true;
		frame_ = parent ? parent->create_child(reactorBinding_, options) : nullptr;
		if (frame_) frame_->configure_gestures(true, true);
		return frame_ != nullptr;
	}

	void ImageCanvas::set_dpi(const unsigned int dpi)
	{
		const unsigned int value = dpi ? dpi : ui::default_dpi;
		if (dpi_ == value && textFont_) return;
		dpi_ = value;

		textFont_ = platform::create_message_font(dpi_);

		const int detailSize = metric(32);
		for (size_t index = 0; index < typeIcons_.size(); ++index)
		{
			detailTypeIcons_[index] = typeIcons_[index];
			while (detailTypeIcons_[index].width / 2 >= detailSize &&
				detailTypeIcons_[index].height / 2 >= detailSize)
				detailTypeIcons_[index] = files::downsample_bgra_2x(detailTypeIcons_[index]);
		}

		reset_thumbnail_cache();
		rebuild_layout();
		queue_thumbnails();
		invalidate();
	}

	void ImageCanvas::reset_thumbnail_cache()
	{
		thumbnailsComplete_ = 0;
		thumbnailTarget_ = static_cast<size_t>(std::ranges::count_if(items_,
			[](const auto& item) { return has_thumbnail(item.kind); }));
		thumbnails_.assign(items_.size(), {});
		thumbnailStates_.assign(items_.size(), ThumbnailState::none);
		thumbnailResidency_.clear();
		for (size_t index = 0; index < items_.size(); ++index)
			if (!has_thumbnail(items_[index].kind)) thumbnailStates_[index] = ThumbnailState::skipped;
		++generation_;
	}

	bool ImageCanvas::load(const std::filesystem::path& path)
	{
		retain_displayed_image();
		const auto item = std::ranges::find_if(items_, [&path](const files::FolderItem& value)
		{
			return paths::equal(value.path, path);
		});
		displayedKind_ = item == items_.end()
			                 ? files::classify(path, std::filesystem::is_directory(files::native_path(path)))
			                 : item->kind;
		const recti client = frame_ ? frame_->client_rect() : recti{};
		const int width = primaryView_.bounds.width > 0
			                  ? primaryView_.bounds.width
			                  : (std::max)(1, client.width);
		const int height = primaryView_.bounds.height > 0
			                   ? primaryView_.bounds.height
			                   : (std::max)(1, client.height);
		initialImage_ = {};
		fullImage_ = {};
		halfImage_ = {};
		quarterImage_ = {};
		fullSizePending_ = false;
		decodePending_ = false;
		decodeFailed_ = false;
		imageScroll_.offset = 0;
		path_ = path;
		primaryView_.sourceWidth = item == items_.end() ? 0 : item->width;
		primaryView_.sourceHeight = item == items_.end() ? 0 : item->height;
		primaryView_.zoom.carry_to_source(
			{primaryView_.sourceWidth, primaryView_.sourceHeight}, primaryView_.bounds);
		if (displayedKind_ != files::ItemKind::image)
		{
			++imageGeneration_;
			pendingImage_.reset();
			initialImage_ = typeIcons_[static_cast<size_t>(displayedKind_)];
			if (displayedKind_ == files::ItemKind::video && item != items_.end())
			{
				const auto index = static_cast<size_t>(item - items_.begin());
				if (index < thumbnails_.size() && !thumbnails_[index].pixels.empty())
				{
					const auto& thumbnail = thumbnails_[index];
					initialImage_ = {thumbnail.width, thumbnail.height, thumbnail.pixels,
						thumbnail.originalWidth, thumbnail.originalHeight};
				}
			}
			primaryView_.sourceWidth = initialImage_.width;
			primaryView_.sourceHeight = initialImage_.height;
			primaryView_.zoom.set_source({primaryView_.sourceWidth, primaryView_.sourceHeight});
			rebuild_properties();
			rebuild_layout();
			invalidate();
			return !initialImage_.pixels.empty();
		}
		const std::uint64_t requestGeneration = ++imageGeneration_;
		const int requiredWidth = primaryView_.sourceWidth > 0 ? (std::min)(width, primaryView_.sourceWidth) : width;
		const int requiredHeight = primaryView_.sourceHeight > 0 ? (std::min)(height, primaryView_.sourceHeight) : height;
		primaryLoading_.begin(requestGeneration, requiredWidth, requiredHeight,
		                      primaryView_.sourceWidth > 0 && primaryView_.sourceHeight > 0);
		const bool restored = restore_retained_image(path, requestGeneration);
		const auto thumbnail = std::ranges::find_if(files_, [&path](const std::filesystem::path& value)
		{
			return paths::equal(value, path);
		});
		if (!restored && thumbnail != files_.end())
		{
			const auto& cached = thumbnails_[static_cast<size_t>(thumbnail - files_.begin())];
			if (!cached.pixels.empty())
			{
				initialImage_ = {
					cached.width, cached.height, cached.pixels,
					cached.originalWidth, cached.originalHeight
				};
				primaryView_.sourceWidth = cached.originalWidth;
				primaryView_.sourceHeight = cached.originalHeight;
				primaryView_.zoom.set_source({primaryView_.sourceWidth, primaryView_.sourceHeight});
				primaryLoading_.seed_thumbnail(cached.width, cached.height);
			}
		}
		rebuild_properties();
		decodePending_ = primaryLoading_.needs_more();
		if (decodePending_) request_image({path, requestGeneration, width, height, false, false});
		rebuild_layout();
		invalidate();
		return true;
	}

	void ImageCanvas::invalidate_file(const std::filesystem::path& path)
	{
		if (path.empty()) return;
		std::erase_if(retainedImages_, [&path](const RetainedImage& image)
		{
			return paths::equal(image.path, path);
		});
		if (!paths::equal(path_, path)) return;
		initialImage_ = {};
		fullImage_ = {};
		halfImage_ = {};
		quarterImage_ = {};
		++imageGeneration_;
		pendingImage_.reset();
		decodePending_ = false;
		const auto current = files::snapshot_file(path);
		if (current && current->exists)
		{
			reset_thumbnail_cache();
			load(path);
		}
		else
		{
			path_.clear();
			primaryView_.sourceWidth = primaryView_.sourceHeight = 0;
			primaryView_.zoom.set_source({});
			rebuild_properties();
			rebuild_layout();
			invalidate();
		}
	}

	void ImageCanvas::invalidate_all_files()
	{
		retainedImages_.clear();
		metadataCache_.clear();
		++metadataGeneration_;
		reset_thumbnail_cache();
		const auto current = path_;
		if (!current.empty()) invalidate_file(current);
		else rebuild_properties();
		invalidate();
	}

	void ImageCanvas::retain_displayed_image()
	{
		if (path_.empty() || displayedKind_ != files::ItemKind::image ||
			(initialImage_.pixels.empty() && fullImage_.pixels.empty()))
			return;
		const auto existing = std::ranges::find_if(retainedImages_, [this](const RetainedImage& value)
		{
			return paths::equal(value.path, path_);
		});
		if (existing != retainedImages_.end()) retainedImages_.erase(existing);
		retainedImages_.push_back({
			path_, std::move(initialImage_), std::move(fullImage_), std::move(halfImage_), std::move(quarterImage_)
		});
		trim_retained_images();
	}

	bool ImageCanvas::restore_retained_image(const std::filesystem::path& path, const std::uint64_t generation)
	{
		const auto found = std::ranges::find_if(retainedImages_, [&path](const RetainedImage& value)
		{
			return paths::equal(value.path, path);
		});
		if (found == retainedImages_.end()) return false;
		initialImage_ = std::move(found->initial);
		fullImage_ = std::move(found->full);
		halfImage_ = std::move(found->half);
		quarterImage_ = std::move(found->quarter);
		retainedImages_.erase(found);
		const auto* best = !fullImage_.pixels.empty() ? &fullImage_ : &initialImage_;
		primaryView_.sourceWidth = best->originalWidth ? best->originalWidth : best->width;
		primaryView_.sourceHeight = best->originalHeight ? best->originalHeight : best->height;
		primaryView_.zoom.set_source({primaryView_.sourceWidth, primaryView_.sourceHeight});
		primaryLoading_.apply(generation, ui::LoadingModel::Phase::source, best->width, best->height);
		return true;
	}

	void ImageCanvas::trim_retained_images()
	{
		constexpr std::uint64_t pixelBudget = 64ull * 1024 * 1024;
		const auto pixels = [](const files::DecodedImage& image)
		{
			return static_cast<std::uint64_t>((std::max)(0, image.width)) * (std::max)(0, image.height);
		};
		std::uint64_t total = 0;
		for (const auto& entry : retainedImages_)
			total += pixels(entry.initial) + pixels(entry.full) + pixels(entry.half) + pixels(entry.quarter);
		while (total > pixelBudget && retainedImages_.size() > 1)
		{
			const auto& entry = retainedImages_.front();
			total -= pixels(entry.initial) + pixels(entry.full) + pixels(entry.half) + pixels(entry.quarter);
			retainedImages_.pop_front();
		}
	}

	void ImageCanvas::set_splitter_position(const int position)
	{
		splitterPosition_ = position;
		rebuild_layout();
		invalidate();
	}

	bool ImageCanvas::image_hit(const pointi point) const
	{
		return !collage_active() && displayed_image() && primaryView_.bounds.contains(point);
	}

	bool ImageCanvas::comparison_hit(const pointi point) const
	{
		return fullscreen_ && !comparisonImage_.pixels.empty() && comparisonView_.bounds.contains(point);
	}

	bool ImageCanvas::inspect_hit(const pointi point) const
	{
		const bool comparison = comparison_hit(point);
		if (!comparison && !image_hit(point)) return false;
		const auto& target = view(comparison);
		return target.zoom.is_fit() && target.zoom.destination(target.bounds).contains(point);
	}

	bool ImageCanvas::immersive_zoom() const
	{
		if (collage_active()) return false;
		const auto exceedsFit = [](const ImageView& value)
		{
			return !value.zoom.is_fit() &&
				value.zoom.effective_scale(value.bounds) > value.zoom.fit_scale(value.bounds) + 1e-9;
		};
		return quickZoom_ || exceedsFit(primaryView_) || (fullscreen_ && exceedsFit(comparisonView_));
	}

	double ImageCanvas::fit_scale(const bool comparison) const
	{
		const auto& value = view(comparison);
		return value.zoom.fit_scale(value.bounds);
	}

	double ImageCanvas::effective_scale(const bool comparison) const
	{
		if (quickZoom_ && activeComparison_ == comparison) return quickZoomScale_;
		const auto& value = view(comparison);
		return value.zoom.effective_scale(value.bounds);
	}

	recti ImageCanvas::image_destination(const double scale, const bool comparison) const
	{
		const auto& value = view(comparison);
		const bool quick = quickZoom_ && activeComparison_ == comparison;
		if (!quick) return value.zoom.destination_at_scale(value.bounds, scale);
		const int width = (std::max)(1, static_cast<int>(std::lround(value.sourceWidth * scale)));
		const int height = (std::max)(1, static_cast<int>(std::lround(value.sourceHeight * scale)));
		const double positionX = quickZoomX_;
		const double positionY = quickZoomY_;
		const int x = width > value.bounds.width
			              ? value.bounds.x + static_cast<int>(positionX * (value.bounds.width - width))
			              : value.bounds.x + (value.bounds.width - width) / 2;
		const int y = height > value.bounds.height
			              ? value.bounds.y + static_cast<int>(positionY * (value.bounds.height - height))
			              : value.bounds.y + (value.bounds.height - height) / 2;
		return {x, y, width, height};
	}

	void ImageCanvas::set_zoom(const double scale, const pointi focus)
	{
		const bool comparison = fullscreen_ && activeComparison_;
		if (comparison && comparisonImage_.pixels.empty()) return;
		if (!comparison && (collage_active() || !displayed_image())) return;
		auto& value = view(comparison);
		const auto sourcePoint = value.zoom.source_at(value.bounds, focus);
		value.zoom.set_explicit(scale, value.bounds, focus);
		rebuild_layout();
		value.zoom.set_explicit_at_source(scale, value.bounds, focus, sourcePoint);
		if (zoomModeHandler_) zoomModeHandler_(immersive_zoom());
		if (frame_) frame_->set_cursor(immersive_zoom() ? platform::CursorShape::sizeAll : platform::CursorShape::arrow);
		if (comparison) request_comparison_image(fullscreenSelection_.secondary());
		else request_full_image();
		invalidate();
	}

	void ImageCanvas::zoom_at(const double factor, const pointi focus)
	{
		step_zoom(factor >= 1.0 ? 1 : -1, focus);
	}

	void ImageCanvas::step_zoom(const int direction, const pointi focus)
	{
		const bool comparison = fullscreen_ && activeComparison_;
		if ((comparison && comparisonImage_.pixels.empty()) ||
			(!comparison && (collage_active() || !displayed_image())))
			return;
		auto& value = view(comparison);
		const auto sourcePoint = value.zoom.source_at(value.bounds, focus);
		value.zoom.step(direction, value.bounds, focus);
		if (value.zoom.is_fit())
		{
			rebuild_layout();
		}
		else
		{
			const double target = value.zoom.explicit_scale();
			rebuild_layout();
			value.zoom.set_explicit_at_source(target, value.bounds, focus, sourcePoint);
			if (comparison) request_comparison_image(fullscreenSelection_.secondary());
			else request_full_image();
		}
		if (zoomModeHandler_) zoomModeHandler_(immersive_zoom());
		if (frame_) frame_->set_cursor(immersive_zoom() ? platform::CursorShape::sizeAll : platform::CursorShape::arrow);
		invalidate();
	}

	void ImageCanvas::reset_zoom(const double scale)
	{
		const recti bounds = active_view().bounds;
		set_zoom(scale, {bounds.x + bounds.width / 2, bounds.y + bounds.height / 2});
		active_view().zoom.set_center(0.5, 0.5);
		invalidate();
	}

	void ImageCanvas::pan_by(const int x, const int y)
	{
		auto& value = active_view();
		value.zoom.pan_by(value.bounds, x, y);
		invalidate();
	}

	void ImageCanvas::begin_quick_zoom(const pointi point)
	{
		activeComparison_ = comparison_hit(point);
		if (!image_hit(point) && !activeComparison_) return;
		const recti fittedImage = image_destination(fit_scale(activeComparison_), activeComparison_);
		if (!fittedImage.contains(point)) return;
		quickZoomScale_ = 1.0;
		quickZoomX_ = std::clamp(static_cast<double>(point.x - fittedImage.x) /
		                         (std::max)(1, fittedImage.width), 0.0, 1.0);
		quickZoomY_ = std::clamp(static_cast<double>(point.y - fittedImage.y) /
		                         (std::max)(1, fittedImage.height), 0.0, 1.0);
		quickZoom_ = true;
		if (activeComparison_) request_comparison_image(fullscreenSelection_.secondary());
		else request_full_image();
		rebuild_layout();
		if (zoomModeHandler_) zoomModeHandler_(true);
		if (frame_) frame_->set_cursor(platform::CursorShape::sizeAll);
		invalidate();
	}

	void ImageCanvas::begin_inspect_candidate(const pointi point)
	{
		if (!inspect_hit(point)) return;
		activeComparison_ = comparison_hit(point);
		if (frame_) frame_->set_capture();
		begin_quick_zoom(point);
	}

	void ImageCanvas::commit_quick_zoom()
	{
		if (!quickZoom_) return;
		auto& value = active_view();
		const recti bounds = value.bounds;
		value.zoom.set_explicit(quickZoomScale_, bounds,
		                        {bounds.x + bounds.width / 2, bounds.y + bounds.height / 2});
		value.zoom.set_center(quickZoomX_, quickZoomY_);
		quickZoom_ = false;
		rebuild_layout();
		if (zoomModeHandler_) zoomModeHandler_(immersive_zoom());
		if (frame_) frame_->set_cursor(platform::CursorShape::sizeAll);
		invalidate();
	}

	void ImageCanvas::update_quick_zoom(const pointi point)
	{
		if (!quickZoom_) return;
		const recti bounds = active_view().bounds;
		quickZoomX_ = std::clamp(static_cast<double>(point.x - bounds.x) /
		                         (std::max)(1, bounds.width), 0.0, 1.0);
		quickZoomY_ = std::clamp(static_cast<double>(point.y - bounds.y) /
		                         (std::max)(1, bounds.height), 0.0, 1.0);
		if (frame_) frame_->set_cursor(platform::CursorShape::zoom);
		invalidate();
	}

	void ImageCanvas::cancel_quick_zoom()
	{
		if (!quickZoom_) return;
		quickZoom_ = false;
		rebuild_layout();
		if (zoomModeHandler_) zoomModeHandler_(immersive_zoom());
		if (frame_) frame_->set_cursor(immersive_zoom() ? platform::CursorShape::sizeAll : platform::CursorShape::arrow);
		invalidate();
	}

	void ImageCanvas::end_quick_zoom()
	{
		if (frame_) frame_->release_capture();
		cancel_quick_zoom();
	}

	void ImageCanvas::set_files(std::vector<std::filesystem::path> files)
	{
		std::vector<files::FolderItem> items;
		items.reserve(files.size());
		for (auto& path : files) items.push_back({std::move(path), files::ItemKind::image});
		const size_t total = items.size();
		set_items(std::move(items), total);
	}

	void ImageCanvas::set_items(std::vector<files::FolderItem> items, const size_t totalItemCount)
	{
		const auto selectedPaths = selected_paths();
		const auto focusedPath = selection_.focus() >= 0 && selection_.focus() < static_cast<int>(files_.size())
			                         ? files_[selection_.focus()]
			                         : std::filesystem::path{};
		items_ = std::move(items);
		files_.clear();
		files_.reserve(items_.size());
		for (const auto& item : items_) files_.push_back(item.path);
		totalItemCount_ = totalItemCount;
		itemBounds_.assign(files_.size(), {});
		reset_thumbnail_cache();
		const auto index_of = [this](const std::filesystem::path& value)
		{
			const auto found = std::ranges::find_if(files_, [&value](const std::filesystem::path& candidate)
			{
				return paths::equal(candidate, value);
			});
			return found == files_.end() ? -1 : static_cast<int>(found - files_.begin());
		};
		std::vector<int> selectedIndices;
		for (const auto& selectedPath : selectedPaths)
			if (const int index = index_of(selectedPath); index >= 0) selectedIndices.push_back(index);
		selection_.restore(static_cast<int>(files_.size()), selectedIndices, index_of(focusedPath));
		fullscreenSelection_.reset(static_cast<int>(files_.size()));
		rebuild_properties();
		rebuild_collage();
		rebuild_layout();
		if (zoomModeHandler_) zoomModeHandler_(immersive_zoom());
		queue_thumbnails();
		if (progressHandler_)
			progressHandler_(mode_ == ItemMode::details || mode_ == ItemMode::single ? thumbnailTarget_ : 0,
			                 thumbnailTarget_);
		invalidate();
	}

	void ImageCanvas::set_mode(const ItemMode mode)
	{
		mode_ = mode;
		rebuild_layout();
		ensure_visible(selection_.focus());
		queue_thumbnails();
		if (zoomModeHandler_) zoomModeHandler_(immersive_zoom());
		if (progressHandler_)
			progressHandler_(mode_ == ItemMode::details || mode_ == ItemMode::single ? thumbnailTarget_ : 0,
			                 thumbnailTarget_);
		invalidate();
	}

	void ImageCanvas::set_fullscreen(const bool fullscreen)
	{
		if (fullscreen_ == fullscreen) return;
		if (fullscreen)
		{
			normalDisplayedPath_ = path_;
			normalZoom_ = primaryView_.zoom;
		}
		fullscreen_ = fullscreen;
		stripOffset_ = 0;
		comparisonImage_ = {};
		comparisonPath_.clear();
		comparisonView_.sourceWidth = comparisonView_.sourceHeight = 0;
		comparisonView_.zoom.set_source({});
		++comparisonGeneration_;
		pendingComparison_.reset();
		if (fullscreen_)
		{
			fullscreenSelection_.reset(static_cast<int>(files_.size()));
			int primary = -1;
			const auto displayed = std::ranges::find_if(files_, [this](const std::filesystem::path& value)
			{
				return paths::equal(value, path_);
			});
			if (displayed != files_.end() && items_[displayed - files_.begin()].kind == files::ItemKind::image)
				primary = static_cast<int>(displayed - files_.begin());
			if (primary < 0)
				for (int index = 0; index < static_cast<int>(items_.size()); ++index)
					if (items_[index].kind == files::ItemKind::image)
					{
						primary = index;
						break;
					}
			if (primary >= 0)
			{
				fullscreenSelection_.compare_select(primary, false);
				load(files_[primary]);
			}
		}
		else
		{
			activeComparison_ = false;
			primaryView_.zoom = normalZoom_;
			if (!normalDisplayedPath_.empty()) load(normalDisplayedPath_);
		}
		rebuild_properties();
		rebuild_collage();
		rebuild_layout();
		queue_thumbnails();
		if (selectionHandler_) selectionHandler_();
		invalidate();
	}

	void ImageCanvas::set_file_properties_visible(const bool visible)
	{
		if (propertiesExpanded_ == visible && comparisonPropertiesExpanded_ == visible) return;
		propertiesExpanded_ = visible;
		comparisonPropertiesExpanded_ = visible;
		rebuild_layout();
		invalidate();
	}

	void ImageCanvas::set_thumbnail_selector_visible(const bool visible)
	{
		if (thumbnailsExpanded_ == visible) return;
		thumbnailsExpanded_ = visible;
		rebuild_layout();
		queue_thumbnails();
		invalidate();
	}

	void ImageCanvas::request_comparison_image(const int index)
	{
		if (index < 0 || index >= static_cast<int>(items_.size()) ||
			items_[index].kind != files::ItemKind::image)
			return;
		const auto requestedPath = files_[index];
		const bool needsFullSize = !comparisonView_.zoom.is_fit() || (quickZoom_ && activeComparison_);
		if (paths::equal(requestedPath, comparisonPath_) && !comparisonImage_.pixels.empty() &&
			(!needsFullSize || (comparisonImage_.width == comparisonImage_.originalWidth &&
				comparisonImage_.height == comparisonImage_.originalHeight)))
			return;
		comparisonPath_ = requestedPath;
		const std::uint64_t requestGeneration = ++comparisonGeneration_;
		comparisonLoading_.begin(requestGeneration, (std::max)(1, comparisonView_.bounds.width),
		                         (std::max)(1, comparisonView_.bounds.height),
		                         comparisonView_.sourceWidth > 0 && comparisonView_.sourceHeight > 0);
		request_image({
			comparisonPath_, requestGeneration,
			(std::max)(1, comparisonView_.bounds.width), (std::max)(1, comparisonView_.bounds.height),
			needsFullSize, true
		});
	}

	void ImageCanvas::select_fullscreen_item(const int index, const bool secondary)
	{
		if (index < 0 || index >= static_cast<int>(items_.size()) || items_[index].kind != files::ItemKind::image)
			return;
		fullscreenSelection_.compare_select(index, secondary);
		if (secondary)
		{
			if (index == fullscreenSelection_.focus())
			{
				comparisonImage_ = {};
				comparisonPath_.clear();
				activeComparison_ = false;
			}
			else
			{
				activeComparison_ = true;
				if (!paths::equal(comparisonPath_, files_[index]))
				{
					comparisonImage_ = {};
					comparisonView_.zoom.set_fit();
					comparisonView_.sourceWidth = comparisonView_.sourceHeight = 0;
					comparisonView_.zoom.set_source({});
				}
				request_comparison_image(index);
			}
		}
		else
		{
			activeComparison_ = false;
			comparisonImage_ = {};
			comparisonPath_.clear();
			comparisonView_.sourceWidth = comparisonView_.sourceHeight = 0;
			comparisonView_.zoom.set_source({});
			load(files_[index]);
		}
		ensure_visible(index);
		notify_selection();
	}

	void ImageCanvas::step_fullscreen_selection(const int direction, const bool secondary)
	{
		int index = secondary ? fullscreenSelection_.secondary() : fullscreenSelection_.focus();
		if (index < 0) index = fullscreenSelection_.focus();
		for (int candidate = index + direction; candidate >= 0 && candidate < static_cast<int>(items_.size());
		     candidate += direction)
			if (items_[candidate].kind == files::ItemKind::image)
			{
				select_fullscreen_item(candidate, secondary);
				return;
			}
	}

	void ImageCanvas::set_sort(const files::SortField field, const bool ascending)
	{
		sortField_ = field;
		sortAscending_ = ascending;
		invalidate();
	}

	void ImageCanvas::set_thumbnail_size(const int size)
	{
		const int value = std::clamp(size, 64, 256);
		if (thumbnailSize_ != value)
		{
			thumbnailSize_ = value;
			const int target = metric(value);
			for (size_t index = 0; index < thumbnails_.size(); ++index)
			{
				if (thumbnailStates_[index] != ThumbnailState::resident) continue;
				const auto& thumbnail = thumbnails_[index];
				if (thumbnail.width < target && thumbnail.height < target &&
					(thumbnail.originalWidth > thumbnail.width || thumbnail.originalHeight > thumbnail.height))
					thumbnailStates_[index] = ThumbnailState::evicted;
			}
		}
		rebuild_layout();
		queue_thumbnails();
		invalidate();
	}

	void ImageCanvas::queue_thumbnails()
	{
		if (!fullscreen_ && (mode_ == ItemMode::details || mode_ == ItemMode::single)) return;
		constexpr size_t maximumBatch = 8;
		constexpr size_t prefetchRadius = 16;
		constexpr size_t residencyBudget = 512;
		const int decodeSize = metric(std::clamp(thumbnailSize_, 64, 256));
		const size_t count = files_.size();
		size_t firstVisible = count;
		size_t lastVisible = 0;
		for (const auto& child : itemFlow_.children)
			if (child.id >= 0 && static_cast<size_t>(child.id) < count && child.bounds.intersects(itemsViewport_))
			{
				const auto index = static_cast<size_t>(child.id);
				firstVisible = (std::min)(firstVisible, index);
				lastVisible = (std::max)(lastVisible, index);
			}
		const bool visibleKnown = firstVisible < count;
		const size_t first = visibleKnown && firstVisible > prefetchRadius ? firstVisible - prefetchRadius : 0;
		const size_t last = visibleKnown ? (std::min)(count - 1, lastVisible + prefetchRadius) : 0;
		std::vector<ThumbnailRequest> batch;
		if (visibleKnown && !thumbnailsInFlight_)
			for (size_t index = first; index <= last && batch.size() < maximumBatch; ++index)
			{
				if (index >= items_.size() || !has_thumbnail(items_[index].kind)) continue;
				const auto state = thumbnailStates_[index];
				if (state != ThumbnailState::none && state != ThumbnailState::evicted) continue;
				thumbnailStates_[index] = ThumbnailState::queued;
				batch.push_back({files_[index], generation_, index, decodeSize, decodeSize});
			}
		// An entry inside the prefetch window must go back on the deque: dropping it leaves the
		// thumbnail resident, untracked and therefore unevictable for the rest of the session.
		for (size_t attempts = thumbnailResidency_.size();
		     visibleKnown && attempts-- && thumbnailResidency_.size() > residencyBudget;)
		{
			const size_t index = thumbnailResidency_.front();
			thumbnailResidency_.pop_front();
			if (index >= thumbnailStates_.size()) continue;
			if (index >= first && index <= last)
			{
				thumbnailResidency_.push_back(index);
				continue;
			}
			if (thumbnailStates_[index] != ThumbnailState::resident) continue;
			thumbnails_[index] = {};
			thumbnailStates_[index] = ThumbnailState::evicted;
		}
		if (!batch.empty())
		{
			thumbnailsInFlight_ = true;
			platform::queue_work(platform::WorkQueue::thumbnail, [this, batch = std::move(batch)]
			{
				for (const auto& request : batch)
				{
					ThumbnailResult result;
					try { result = decode_thumbnail(request); }
					catch (...)
					{
						result = {};
						static_cast<ThumbnailRequest&>(result) = request;
						result.width = result.height = 0;
					}
					platform::queue_ui([this, result = std::move(result)]() mutable
					{
						apply_thumbnail(std::move(result));
					});
				}
				// The batch is only re-primed once its results have landed, so each new batch
				// is chosen from the viewport as it stands then rather than as it was.
				platform::queue_ui([this]
				{
					thumbnailsInFlight_ = false;
					rebuild_layout();
					queue_thumbnails();
					invalidate();
				});
			});
		}
		if (progressHandler_) progressHandler_(thumbnailsComplete_, thumbnailTarget_);
	}

	bool ImageCanvas::collage_active() const
	{
		return !fullscreen_ && selection_.size() > 1;
	}

	void ImageCanvas::rebuild_collage()
	{
		constexpr size_t maximumCells = 24;
		std::vector<int> wanted;
		if (collage_active())
		{
			wanted.reserve((std::min)(maximumCells, selection_.size()));
			for (const int index : selection_.selected())
			{
				if (wanted.size() >= maximumCells) break;
				if (index >= 0 && index < static_cast<int>(files_.size())) wanted.push_back(index);
			}
		}
		// A marquee drag calls this on every mouse move, so an unchanged cell set must not
		// invalidate the decodes already in flight for it. Re-sorting keeps the indices and
		// changes what they mean, so identity is the path, not the index.
		if (std::ranges::equal(wanted, collage_, [this](const int index, const CollageCell& cell)
		{
			return index == cell.index && paths::equal(files_[index], cell.path);
		}))
			return;

		auto previous = std::move(collage_);
		collage_.clear();
		collage_.reserve(wanted.size());
		++collageGeneration_;
		if (wanted.empty())
		{
			collageDecodeSize_ = 0;
			return;
		}
		for (const int index : wanted)
		{
			const auto carried = std::ranges::find_if(previous, [this, index](const CollageCell& cell)
			{
				return cell.state == ThumbnailState::resident && paths::equal(cell.path, files_[index]);
			});
			if (carried != previous.end())
			{
				CollageCell cell = std::move(*carried);
				cell.index = index;
				cell.bounds = {};
				collage_.push_back(std::move(cell));
				continue;
			}
			CollageCell cell;
			cell.index = index;
			cell.path = files_[index];
			cell.kind = items_[index].kind;
			cell.source = {items_[index].width, items_[index].height};
			if (!has_thumbnail(cell.kind))
			{
				cell.image = typeIcons_[static_cast<size_t>(cell.kind)];
				cell.source = {cell.image.width, cell.image.height};
				cell.state = ThumbnailState::skipped;
			}
			else if (static_cast<size_t>(index) < thumbnails_.size() && !thumbnails_[index].pixels.empty())
			{
				// The cached thumbnail stands in until the cell-sized decode lands.
				const auto& thumbnail = thumbnails_[index];
				cell.image = {
					thumbnail.width, thumbnail.height, thumbnail.pixels,
					thumbnail.originalWidth, thumbnail.originalHeight
				};
				if (cell.source.width <= 0 || cell.source.height <= 0)
					cell.source = {thumbnail.originalWidth, thumbnail.originalHeight};
			}
			collage_.push_back(std::move(cell));
		}
		primaryView_.zoom.set_fit();
	}

	void ImageCanvas::arrange_collage()
	{
		if (collage_.empty()) return;
		std::vector<sizei> dimensions;
		dimensions.reserve(collage_.size());
		for (const auto& cell : collage_)
			dimensions.push_back(cell.source.width > 0 && cell.source.height > 0 ? cell.source : sizei{4, 3});
		const auto cells = ui::layout_collage(primaryView_.bounds, dimensions);
		for (size_t index = 0; index < collage_.size(); ++index)
			collage_[index].bounds = index < cells.size() ? cells[index] : recti{};
		queue_collage();
	}

	void ImageCanvas::queue_collage()
	{
		if (collage_.empty() || collageInFlight_) return;
		int longest = 0;
		for (const auto& cell : collage_)
			longest = (std::max)(longest, (std::max)(cell.bounds.width, cell.bounds.height));
		if (longest <= 0) return;
		// One size serves every cell, so the ceiling falls as the cell count rises: a crowded cell is
		// small anyway, and this is what bounds the pixels a collage can hold resident. Growing the
		// selection must lower it again, or 24 new cells inherit the size two cells earned.
		const int ceiling = metric(collage_.size() <= 4 ? 1024 : collage_.size() <= 9 ? 640 : 384);
		const int decodeSize = std::clamp(longest, metric(192), ceiling);
		collageDecodeSize_ = (std::min)(collageDecodeSize_, ceiling);
		if (decodeSize > collageDecodeSize_ * 3 / 2)
		{
			// A materially larger cell deserves a sharper decode; the current image stays on show
			// until the replacement lands. Shrinking never re-decodes, so a resize cannot oscillate.
			collageDecodeSize_ = decodeSize;
			for (auto& cell : collage_)
				if (cell.state == ThumbnailState::resident &&
					(cell.image.originalWidth > cell.image.width ||
						cell.image.originalHeight > cell.image.height))
					cell.state = ThumbnailState::none;
		}
		std::vector<std::pair<size_t, std::filesystem::path>> batch;
		for (size_t index = 0; index < collage_.size(); ++index)
			if (collage_[index].state == ThumbnailState::none)
			{
				collage_[index].state = ThumbnailState::queued;
				batch.emplace_back(index, collage_[index].path);
			}
		if (batch.empty()) return;
		collageInFlight_ = true;
		const std::uint64_t generation = collageGeneration_;
		const int requestSize = collageDecodeSize_;
		platform::queue_work(platform::WorkQueue::thumbnail,
		                     [this, generation, requestSize, batch = std::move(batch)]
		                     {
			                     for (const auto& [cell, path] : batch)
			                     {
				                     files::DecodedImage image;
				                     try { image = files::load_thumbnail(path, requestSize, requestSize); }
				                     catch (...) { image = {}; }
				                     platform::queue_ui(
					                     [this, generation, cell, image = std::move(image)]() mutable
					                     {
						                     apply_collage(generation, cell, std::move(image));
					                     });
			                     }
			                     platform::queue_ui([this]
			                     {
				                     collageInFlight_ = false;
				                     rebuild_layout();
				                     invalidate();
			                     });
		                     });
	}

	void ImageCanvas::apply_collage(const std::uint64_t generation, const size_t cell, files::DecodedImage image)
	{
		if (generation != collageGeneration_ || cell >= collage_.size()) return;
		auto& target = collage_[cell];
		if (image.pixels.empty())
		{
			target.state = ThumbnailState::skipped;
			return;
		}
		if (image.originalWidth > 0 && target.index >= 0 && static_cast<size_t>(target.index) < items_.size())
		{
			items_[target.index].width = image.originalWidth;
			items_[target.index].height = image.originalHeight;
		}
		target.source = image.originalWidth > 0
			                ? sizei{image.originalWidth, image.originalHeight}
			                : sizei{image.width, image.height};
		target.image = std::move(image);
		target.state = ThumbnailState::resident;
		invalidate_image_pane();
	}

	void ImageCanvas::paint_collage(const ui::CanvasRenderer& renderer) const
	{
		const int gap = metric(4);
		for (const auto& cell : collage_)
		{
			const recti frame{
				cell.bounds.x + gap / 2, cell.bounds.y + gap / 2,
				cell.bounds.width - gap, cell.bounds.height - gap
			};
			if (frame.is_empty()) continue;
			if (cell.image.pixels.empty())
			{
				renderer.fill(frame, platform::system_color(platform::SystemColor::face));
			}
			else
			{
				const bool alpha = cell.kind != files::ItemKind::image &&
					typeIconAlpha_[static_cast<size_t>(cell.kind)];
				renderer.image({cell.image.width, cell.image.height, cell.image.pixels, alpha},
				               ui::fit_centered({cell.image.width, cell.image.height}, frame));
			}
			if (cell.index == selection_.focus())
				renderer.outline(frame, platform::system_color(platform::SystemColor::highlight), metric(2));
		}
	}

	void ImageCanvas::request_image(ImageRequest request)
	{
		(request.comparison ? pendingComparison_ : pendingImage_) = std::move(request);
		pump_image();
	}

	void ImageCanvas::pump_image()
	{
		if (imageInFlight_) return;
		std::optional<ImageRequest> request;
		if (pendingImage_) request = std::exchange(pendingImage_, std::nullopt);
		else if (pendingComparison_) request = std::exchange(pendingComparison_, std::nullopt);
		else return;
		imageInFlight_ = true;
		platform::queue_work(platform::WorkQueue::image, [this, request = std::move(*request)]
		{
			ImageResult result;
			static_cast<ImageRequest&>(result) = request;
			try
			{
				result.image = result.fullSize
					               ? files::load_image(result.path)
					               : files::load_image_for_area(result.path, result.width, result.height);
				if (result.fullSize && !result.comparison && !result.image.pixels.empty())
				{
					const double fitScale = (std::min)(
						static_cast<double>(result.width) / result.image.width,
						static_cast<double>(result.height) / result.image.height);
					if (fitScale <= 0.25)
					{
						auto half = files::downsample_bgra_2x(result.image);
						result.quarter = files::downsample_bgra_2x(half);
					}
					else if (fitScale <= 0.5) result.half = files::downsample_bgra_2x(result.image);
				}
			}
			catch (...)
			{
				result.image = {};
				result.half = {};
				result.quarter = {};
			}
			platform::queue_ui([this, result = std::move(result)]() mutable { apply_image(std::move(result)); });
		});
	}

	void ImageCanvas::pump_metadata(MetadataJob job)
	{
		if (job.generation != metadataGeneration_) return;
		if (informationProgressHandler_) informationProgressHandler_(job.offset, job.paths.size());
		if (job.offset >= job.paths.size())
		{
			apply_metadata(job);
			rebuild_layout();
			invalidate();
			return;
		}
		platform::queue_work(platform::WorkQueue::metadata, [this, job = std::move(job)]() mutable
		{
			constexpr size_t chunk = 8;
			const size_t end = (std::min)(job.offset + chunk, job.paths.size());
			for (; job.offset < end; ++job.offset)
			{
				files::Metadata value;
				try { value = files::read_metadata(job.paths[job.offset]); }
				catch (...) {}
				job.values.push_back(std::move(value));
			}
			platform::queue_ui([this, job = std::move(job)]() mutable { pump_metadata(std::move(job)); });
		});
	}

	ImageCanvas::ThumbnailResult ImageCanvas::decode_thumbnail(const ThumbnailRequest& request)
	{
		ThumbnailResult result;
		static_cast<ThumbnailRequest&>(result) = request;
		auto image = files::load_thumbnail(result.path, result.width, result.height);
		result.width = image.width;
		result.height = image.height;
		result.pixels = std::move(image.pixels);
		result.originalWidth = image.originalWidth;
		result.originalHeight = image.originalHeight;
		return result;
	}

	void ImageCanvas::apply_thumbnail(ThumbnailResult result)
	{
		if (result.generation != generation_ || result.index >= thumbnails_.size()) return;
		if (result.originalWidth > 0 && result.index < items_.size())
		{
			items_[result.index].width = result.originalWidth;
			items_[result.index].height = result.originalHeight;
		}
		thumbnails_[result.index] = {
			result.width, result.height, std::move(result.pixels),
			result.originalWidth, result.originalHeight
		};
		if (thumbnailStates_[result.index] != ThumbnailState::resident)
			thumbnailsComplete_ = (std::min)(thumbnailsComplete_ + 1, thumbnailTarget_);
		thumbnailStates_[result.index] = ThumbnailState::resident;
		thumbnailResidency_.push_back(result.index);
		if (displayedKind_ == files::ItemKind::video && paths::equal(path_, result.path) &&
			!thumbnails_[result.index].pixels.empty())
		{
			const auto& thumbnail = thumbnails_[result.index];
			initialImage_ = {thumbnail.width, thumbnail.height, thumbnail.pixels,
				thumbnail.originalWidth, thumbnail.originalHeight};
			primaryView_.sourceWidth = thumbnail.width;
			primaryView_.sourceHeight = thumbnail.height;
			primaryView_.zoom.set_source({thumbnail.width, thumbnail.height});
			rebuild_layout();
			invalidate();
		}
		invalidate_item(static_cast<int>(result.index));
	}

	void ImageCanvas::apply_image(ImageResult result)
	{
		imageInFlight_ = false;
		const bool comparison = result.comparison;
		// A superseded full-size decode still ends the pending request that blocks the next one.
		if (result.fullSize && !comparison) fullSizePending_ = false;
		bool changed = false;
		if (!comparison && result.generation == imageGeneration_)
		{
			if (result.image.pixels.empty())
			{
				if (primaryLoading_.fail(result.generation, ui::LoadingModel::Failure::unreadable))
				{
					decodePending_ = false;
					decodeFailed_ = true;
					changed = true;
				}
			}
			else
			{
				const bool decodedFullSize = result.image.width == result.image.originalWidth &&
					result.image.height == result.image.originalHeight;
				primaryLoading_.cap_need_to_source(result.image.originalWidth, result.image.originalHeight);
				if (!primaryLoading_.apply(result.generation, ui::LoadingModel::Phase::source,
				                           result.image.width, result.image.height))
				{
					pump_image();
					return;
				}
				decodePending_ = primaryLoading_.needs_more();
				decodeFailed_ = false;
				if (result.fullSize || decodedFullSize)
				{
					fullImage_ = std::move(result.image);
					halfImage_ = std::move(result.half);
					quarterImage_ = std::move(result.quarter);
				}
				else initialImage_ = std::move(result.image);
				const auto& dimensions = result.fullSize || decodedFullSize ? fullImage_ : initialImage_;
				primaryView_.sourceWidth = dimensions.originalWidth ? dimensions.originalWidth : dimensions.width;
				primaryView_.sourceHeight = dimensions.originalHeight ? dimensions.originalHeight : dimensions.height;
				primaryView_.zoom.set_source({primaryView_.sourceWidth, primaryView_.sourceHeight});
				path_ = result.path;
				const auto item = std::ranges::find_if(files_, [this](const std::filesystem::path& value)
				{
					return paths::equal(value, path_);
				});
				if (item != files_.end())
				{
					const auto index = static_cast<size_t>(item - files_.begin());
					items_[index].width = primaryView_.sourceWidth;
					items_[index].height = primaryView_.sourceHeight;
				}
				rebuild_properties();
				changed = true;
				if (!primaryView_.zoom.is_fit() || quickZoom_) request_full_image();
			}
		}
		else if (comparison && result.generation == comparisonGeneration_)
		{
			if (result.image.pixels.empty())
				changed = comparisonLoading_.fail(result.generation, ui::LoadingModel::Failure::unreadable);
			else
			{
				comparisonLoading_.cap_need_to_source(result.image.originalWidth, result.image.originalHeight);
				if (comparisonLoading_.apply(result.generation, ui::LoadingModel::Phase::source,
			                                  result.image.width, result.image.height))
				{
				comparisonImage_ = std::move(result.image);
				comparisonView_.sourceWidth = comparisonImage_.originalWidth
					                              ? comparisonImage_.originalWidth
					                              : comparisonImage_.width;
				comparisonView_.sourceHeight = comparisonImage_.originalHeight
					                               ? comparisonImage_.originalHeight
					                               : comparisonImage_.height;
				comparisonView_.zoom.set_source({comparisonView_.sourceWidth, comparisonView_.sourceHeight});
				changed = true;
				}
			}
		}
		pump_image();
		if (changed)
		{
			rebuild_layout();
			queue_thumbnails();
			invalidate();
		}
	}

	void ImageCanvas::set_fit(const bool fit)
	{
		const bool comparison = fullscreen_ && activeComparison_;
		// A collage has no one image to scale, and an explicit scale would size the pane to the
		// focused image instead of to the cells.
		if (!comparison && collage_active()) return;
		auto& value = view(comparison);
		if (fit)
		{
			value.zoom.set_fit();
			imageScroll_.offset = 0;
		}
		else value.zoom.set_explicit(1.0, value.bounds,
		                             {value.bounds.x + value.bounds.width / 2,
		                              value.bounds.y + value.bounds.height / 2});
		if (!fit)
		{
			if (fullscreen_ && activeComparison_) request_comparison_image(fullscreenSelection_.secondary());
			else request_full_image();
		}
		rebuild_layout();
		if (zoomModeHandler_) zoomModeHandler_(immersive_zoom());
		queue_thumbnails();
		invalidate();
	}

	void ImageCanvas::request_full_image()
	{
		if (path_.empty() || !fullImage_.pixels.empty() || fullSizePending_) return;
		primaryLoading_.require(primaryView_.sourceWidth, primaryView_.sourceHeight);
		fullSizePending_ = true;
		request_image({
			path_, imageGeneration_, (std::max)(1, primaryView_.bounds.width),
			(std::max)(1, primaryView_.bounds.height), true, false
		});
	}

	const files::DecodedImage* ImageCanvas::displayed_image() const
	{
		const bool needsFull = quickZoom_ || !primaryView_.zoom.is_fit();
		if (needsFull && !fullImage_.pixels.empty()) return &fullImage_;
		const double scale = effective_scale(false);
		const int neededWidth = (std::max)(1, static_cast<int>(std::lround(primaryView_.sourceWidth * scale)));
		const int neededHeight = (std::max)(1, static_cast<int>(std::lround(primaryView_.sourceHeight * scale)));
		for (const auto* candidate : {&quarterImage_, &halfImage_, &initialImage_, &fullImage_})
			if (!candidate->pixels.empty() && candidate->width >= neededWidth && candidate->height >= neededHeight)
				return candidate;
		const files::DecodedImage* largest = nullptr;
		for (const auto* candidate : {&initialImage_, &quarterImage_, &halfImage_, &fullImage_})
			if (!candidate->pixels.empty() && (!largest ||
				static_cast<size_t>(candidate->width) * candidate->height >
				static_cast<size_t>(largest->width) * largest->height))
				largest = candidate;
		return largest;
	}

	void ImageCanvas::zoom(const double factor)
	{
		const recti bounds = active_view().bounds;
		step_zoom(factor >= 1.0 ? 1 : -1, {bounds.x + bounds.width / 2, bounds.y + bounds.height / 2});
	}

	bool ImageCanvas::cancel_transient_state()
	{
		if (quickZoom_)
		{
			end_quick_zoom();
			return true;
		}
		if (selectingZoom_ || panningImage_)
		{
			selectingZoom_ = false;
			panningImage_ = false;
			if (frame_) frame_->release_capture();
			invalidate();
			return true;
		}
		return false;
	}

	platform::MessageResult ImageCanvas::message(const platform::WindowFramePtr& frame,
	                                             const platform::WindowMessage message)
	{
		frame_ = frame;
		if (message == platform::WindowMessage::close)
		{
			frame->close();
			return 0;
		}
		if (message == platform::WindowMessage::captureLost)
			return handle_input(InputEvent::captureLost);
		return 0;
	}

	platform::MessageResult ImageCanvas::files_dropped(const platform::WindowFramePtr& frame,
	                                                   const platform::FileDrop& drop)
	{
		frame_ = frame;
		if (dropHandler_) dropHandler_(drop.files);
		return 0;
	}

	platform::MessageResult ImageCanvas::mouse(const platform::WindowFramePtr& frame,
	                                           const platform::MouseMessage message,
	                                           const platform::MouseInput& input)
	{
		frame_ = frame;
		InputEvent event{};
		switch (message)
		{
		case platform::MouseMessage::leftButtonDown: event = InputEvent::leftButtonDown;
			break;
		case platform::MouseMessage::leftButtonDoubleClick: event = InputEvent::leftButtonDoubleClick;
			break;
		case platform::MouseMessage::rightButtonDown: event = InputEvent::rightButtonDown;
			break;
		case platform::MouseMessage::leftButtonUp: event = InputEvent::leftButtonUp;
			break;
		case platform::MouseMessage::move: event = InputEvent::move;
			break;
		case platform::MouseMessage::wheel: event = InputEvent::wheel;
			break;
		case platform::MouseMessage::leave: event = InputEvent::leave;
			break;
		case platform::MouseMessage::contextMenu: event = InputEvent::contextMenu;
			break;
		}
		return handle_input(event, input);
	}

	platform::MessageResult ImageCanvas::key(const platform::WindowFramePtr& frame,
	                                         const platform::KeyMessage message,
	                                         const platform::KeyInput& input)
	{
		frame_ = frame;
		return message == platform::KeyMessage::down ? handle_input(InputEvent::keyDown, {}, input) : 0;
	}

	platform::MessageResult ImageCanvas::gesture(const platform::WindowFramePtr& frame,
	                                             const platform::GestureInput& input)
	{
		frame_ = frame;
		if (input.kind == platform::GestureKind::zoom)
		{
			if (input.begin)
			{
				activeComparison_ = comparison_hit(input.point);
				gestureZoomDistance_ = input.argument;
				const auto& active = active_view();
				gestureZoomStart_ = active.zoom.effective_scale(active.bounds);
			}
			else if (gestureZoomDistance_)
				set_zoom(gestureZoomStart_ * input.argument / gestureZoomDistance_, input.point);
		}
		else if (input.begin) gesturePanPoint_ = input.point;
		else if (!active_view().zoom.is_fit())
		{
			pan_by(input.point.x - gesturePanPoint_.x, input.point.y - gesturePanPoint_.y);
			gesturePanPoint_ = input.point;
		}
		return 0;
	}

	void ImageCanvas::size(const platform::WindowFramePtr& frame, const sizei extent,
	                       platform::MeasureContext&)
	{
		frame_ = frame;
		if (splitterPosition_ < 0) splitterPosition_ = ui::unscale_metric(extent.width * 3 / 5, dpi_);
		rebuild_layout();
		queue_thumbnails();
		frame->invalidate();
	}

	platform::MessageResult ImageCanvas::handle_input(const InputEvent event,
	                                                  const platform::MouseInput& mouse,
	                                                  const platform::KeyInput& key)
	{
		switch (event)
		{
		case InputEvent::leftButtonDown:
			{
				if (frame_) frame_->set_focus();
				const pointi point = mouse.point;
				const bool control = mouse.control;
				const bool shift = mouse.shift;
				if (stripHeaderBounds_.contains(point))
				{
					if (commandHandlers_.toggleThumbnails) commandHandlers_.toggleThumbnails();
					else set_thumbnail_selector_visible(!thumbnailsExpanded_);
					return 0;
				}
				if (propertyHeaderBounds_.contains(point))
				{
					if (fullscreen_)
					{
						propertiesExpanded_ = !propertiesExpanded_;
						rebuild_layout();
						invalidate();
					}
					else if (commandHandlers_.toggleProperties) commandHandlers_.toggleProperties();
					else set_file_properties_visible(!file_properties_visible());
					return 0;
				}
				if (comparisonPropertyHeaderBounds_.contains(point))
				{
					comparisonPropertiesExpanded_ = !comparisonPropertiesExpanded_;
					rebuild_layout();
					invalidate();
					return 0;
				}
				if (fullscreen_ && thumbnailScroll_.visible() && thumbnailScroll_.track.contains(point))
				{
					if (thumbnailScroll_.thumb.contains(point))
					{
						scrollDrag_ = ScrollDrag::thumbnails;
						scrollDragOffset_ = point.x - thumbnailScroll_.thumb.x;
						if (frame_) frame_->set_capture();
					}
					else
					{
						stripOffset_ = std::clamp(stripOffset_ +
						                          (point.x < thumbnailScroll_.thumb.x
							                           ? -itemsViewport_.width
							                           : itemsViewport_.width),
						                          0, thumbnailScroll_.maximum());
						rebuild_layout();
						invalidate();
					}
					return 0;
				}
				if (fullscreen_ && !immersive_zoom())
				{
					const int fullscreenItem = hit_test_item(point);
					if (fullscreenItem >= 0)
					{
						select_fullscreen_item(fullscreenItem, control);
						return 0;
					}
				}
				if (files_.empty() && totalItemCount_ > 0 && itemsViewport_.contains(point) && clearFilterHandler_)
				{
					clearFilterHandler_();
					return 0;
				}
				if (image_hit(point) || comparison_hit(point))
				{
					activeComparison_ = comparison_hit(point);
					if (control)
					{
						selectingZoom_ = true;
						zoomSelectionStart_ = zoomSelectionCurrent_ = point;
						if (frame_) frame_->set_capture();
					}
					else if (shift)
						step_zoom(1, point);
					else if (!active_view().zoom.is_fit())
					{
						panningImage_ = true;
						imageDragOrigin_ = point;
						imageDragPoint_ = {};
						if (frame_) frame_->set_capture();
						if (frame_) frame_->set_cursor(platform::CursorShape::sizeAll);
					}
					else begin_inspect_candidate(point);
					return 0;
				}
				const auto beginScroll = [this, point](ui::VerticalScroll& scroll, const ScrollDrag target)
				{
					if (!scroll.visible() || !scroll.track.contains(point)) return false;
					if (scroll.thumb.contains(point))
					{
						scrollDrag_ = target;
						scrollDragOffset_ = point.y - scroll.thumb.y;
						if (frame_) frame_->set_capture();
					}
					else
					{
						scroll.scroll_by(point.y < scroll.thumb.y ? -scroll.viewportExtent : scroll.viewportExtent);
						rebuild_layout();
						queue_thumbnails();
						invalidate();
					}
					return true;
				};
				if (beginScroll(imageScroll_, ScrollDrag::image) || beginScroll(itemsScroll_, ScrollDrag::items))
					return
						0;
				if (mode_ != ItemMode::single && splitterBounds_.contains(point))
				{
					draggingSplitter_ = true;
					if (frame_) frame_->set_capture();
					return 0;
				}
				if (mode_ == ItemMode::details && detailsHeaderBounds_.contains(point))
				{
					const auto columns = media_detail_columns(detailsHeaderBounds_);
					constexpr std::array fields{
						files::SortField::name, files::SortField::modified, files::SortField::type,
						files::SortField::size, files::SortField::dimensions, files::SortField::duration
					};
					for (size_t index = 0; index < columns.size(); ++index)
						if (columns[index].contains(point) && sortHandler_)
						{
							sortHandler_(fields[index], sortField_ == fields[index] ? !sortAscending_ : true);
							break;
						}
					return 0;
				}
				const int index = hit_test_item(point);
				pressedIndex_ = index;
				if (index >= 0)
				{
					fileDragStart_ = point;
					deferSingleSelection_ = !control && !shift && active_selection().contains(index) &&
						active_selection().size() > 1;
					if (!deferSingleSelection_) active_selection().click(index, control, shift);
					notify_selection();
					if (items_[index].kind != files::ItemKind::folder &&
						!media::is_media(items_[index].kind) && activationHandler_)
						activationHandler_(files_[index]);
					else load(files_[index]);
					if (frame_) frame_->set_capture();
				}
				else
				{
					draggingMarquee_ = true;
					marqueeStart_ = marqueeCurrent_ = point;
					active_selection().begin_marquee(control || shift);
					active_selection().marquee(itemFlow_.children, {point.x, point.y, 0, 0});
					notify_selection();
					if (frame_) frame_->set_capture();
				}
				invalidate();
				return 0;
			}
		case InputEvent::leftButtonDoubleClick:
			{
				const int index = hit_test_item(mouse.point);
				if (index >= 0 && activationHandler_) activationHandler_(files_[index]);
				return 0;
			}
		case InputEvent::rightButtonDown:
			{
				const pointi point = mouse.point;
				if (mouse.shift && (image_hit(point) || comparison_hit(point)))
				{
					activeComparison_ = comparison_hit(point);
					suppressContextMenu_ = true;
					step_zoom(-1, point);
					return 0;
				}
				const int index = hit_test_item(point);
				if (index >= 0 && !active_selection().contains(index))
				{
					active_selection().click(index, false, false);
					notify_selection();
					invalidate();
				}
				return 0;
			}
		case InputEvent::move:
			{
				const int x = mouse.point.x;
				const int y = mouse.point.y;
				if (frame_) frame_->track_mouse_leave();
				if (quickZoom_)
				{
					update_quick_zoom({x, y});
					return 0;
				}
				if (panningImage_)
				{
					const int ramp = metric(120);
					const pointi offset{
						ui::ZoomModel::accelerated_pan_offset(x - imageDragOrigin_.x, ramp),
						ui::ZoomModel::accelerated_pan_offset(y - imageDragOrigin_.y, ramp)
					};
					pan_by(offset.x - imageDragPoint_.x, offset.y - imageDragPoint_.y);
					imageDragPoint_ = offset;
					if (frame_) frame_->set_cursor(platform::CursorShape::sizeAll);
					return 0;
				}
				if (selectingZoom_)
				{
					zoomSelectionCurrent_ = {x, y};
					invalidate();
					return 0;
				}
				if (pressedIndex_ >= 0 && mouse.leftButton)
				{
					const auto threshold = platform::drag_threshold();
					if (std::abs(x - fileDragStart_.x) >= threshold.width ||
						std::abs(y - fileDragStart_.y) >= threshold.height)
						draggingFiles_ = true;
					if (draggingFiles_)
					{
						auto paths = selected_paths();
						draggingFiles_ = false;
						deferSingleSelection_ = false;
						pressedIndex_ = -1;
						if (frame_) frame_->release_capture();
						if (fileDragHandler_ && !paths.empty()) fileDragHandler_(std::move(paths));
						return 0;
					}
				}
				if (scrollDrag_ != ScrollDrag::none)
				{
					if (scrollDrag_ == ScrollDrag::thumbnails)
					{
						thumbnailScroll_.set_thumb_position(x - scrollDragOffset_);
						stripOffset_ = thumbnailScroll_.offset;
						rebuild_layout();
						queue_thumbnails();
						invalidate();
						return 0;
					}
					auto& scroll = scrollDrag_ == ScrollDrag::image ? imageScroll_ : itemsScroll_;
					scroll.set_thumb_position(y - scrollDragOffset_);
					rebuild_layout();
					queue_thumbnails();
					invalidate();
					return 0;
				}
				if (draggingSplitter_)
				{
					const recti client = frame_ ? frame_->client_rect() : recti{};
					const int minimumPane = metric(180);
					const int physicalPosition = std::clamp(
						x, minimumPane, (std::max)(minimumPane, client.width - minimumPane));
					splitterPosition_ = ui::unscale_metric(physicalPosition, dpi_);
					rebuild_layout();
					invalidate();
					return 0;
				}
				if (draggingMarquee_)
				{
					marqueeCurrent_ = {x, y};
					const recti area{
						(std::min)(marqueeStart_.x, marqueeCurrent_.x),
						(std::min)(marqueeStart_.y, marqueeCurrent_.y), std::abs(marqueeCurrent_.x - marqueeStart_.x),
						std::abs(marqueeCurrent_.y - marqueeStart_.y)
					};
					active_selection().marquee(itemFlow_.children, area);
					notify_selection();
					invalidate();
					return 0;
				}
				hoverIndex_ = hit_test_item({x, y});
				const bool splitterHover = mode_ != ItemMode::single && splitterBounds_.contains({x, y});
				if (hoveringSplitter_ != splitterHover)
					hoveringSplitter_ = splitterHover;
				if (frame_)
				{
					if (splitterHover) frame_->set_cursor(platform::CursorShape::sizeHorizontal);
					else if (immersive_zoom()) frame_->set_cursor(platform::CursorShape::sizeAll);
					else if (inspect_hit({x, y})) frame_->set_cursor(platform::CursorShape::zoom);
					else frame_->set_cursor(platform::CursorShape::arrow);
				}
				invalidate();
				return 0;
			}
		case InputEvent::leave:
			hoverIndex_ = -1;
			hoveringSplitter_ = false;
			invalidate();
			return 0;
		case InputEvent::leftButtonUp:
			if (quickZoom_)
			{
				end_quick_zoom();
				return 0;
			}
			if (panningImage_)
			{
				panningImage_ = false;
				if (frame_) frame_->release_capture();
				return 0;
			}
			if (selectingZoom_)
			{
				selectingZoom_ = false;
				if (frame_) frame_->release_capture();
				const recti selection{
					(std::min)(zoomSelectionStart_.x, zoomSelectionCurrent_.x),
					(std::min)(zoomSelectionStart_.y, zoomSelectionCurrent_.y),
					std::abs(zoomSelectionCurrent_.x - zoomSelectionStart_.x),
					std::abs(zoomSelectionCurrent_.y - zoomSelectionStart_.y)
				};
				if (selection.width >= metric(8) && selection.height >= metric(8))
				{
					const pointi center{selection.x + selection.width / 2, selection.y + selection.height / 2};
					const auto& active = active_view();
					const recti bounds = active.bounds;
					const double scale = active.zoom.effective_scale(active.bounds) *
						(std::min)(static_cast<double>(bounds.width) / selection.width,
						           static_cast<double>(bounds.height) / selection.height);
					set_zoom(scale, center);
					pan_by(bounds.x + bounds.width / 2 - center.x, bounds.y + bounds.height / 2 - center.y);
				}
				invalidate();
				return 0;
			}
			if (scrollDrag_ != ScrollDrag::none)
			{
				scrollDrag_ = ScrollDrag::none;
				if (frame_) frame_->release_capture();
			}
			if (draggingSplitter_)
			{
				draggingSplitter_ = false;
				if (frame_) frame_->release_capture();
			}
			if (draggingMarquee_)
			{
				draggingMarquee_ = false;
				if (frame_) frame_->release_capture();
			}
			if (deferSingleSelection_ && pressedIndex_ >= 0)
			{
				active_selection().click(pressedIndex_, false, false);
				notify_selection();
			}
			deferSingleSelection_ = false;
			pressedIndex_ = -1;
			if (frame_) frame_->release_capture();
			invalidate();
			return 0;
		case InputEvent::captureLost:
			if (quickZoom_) end_quick_zoom();
			panningImage_ = false;
			selectingZoom_ = false;
			draggingFiles_ = false;
			draggingSplitter_ = false;
			draggingMarquee_ = false;
			scrollDrag_ = ScrollDrag::none;
			return 0;
		case InputEvent::wheel:
			{
				const pointi point = mouse.point;
				if (fullscreen_ && !immersive_zoom() && itemsViewport_.contains({point.x, point.y}))
				{
					stripOffset_ = std::clamp(stripOffset_ - mouse.wheelDelta / 120 * 96,
					                          0, thumbnailScroll_.maximum());
					rebuild_layout();
					queue_thumbnails();
					invalidate();
					return 0;
				}
				if (quickZoom_)
				{
					quickZoomScale_ = std::clamp(quickZoomScale_ *
					                             (mouse.wheelDelta > 0 ? 1.25 : 0.8), 0.05, 16.0);
					update_quick_zoom({point.x, point.y});
					return 0;
				}
				if (mouse.control)
				{
					activeComparison_ = comparison_hit({point.x, point.y});
					step_zoom(mouse.wheelDelta > 0 ? 1 : -1, {point.x, point.y});
					return 0;
				}
				if (immersive_zoom())
				{
					step_focus(mouse.wheelDelta > 0 ? -1 : 1);
					return 0;
				}
				if (image_hit({point.x, point.y}))
				{
					step_focus(mouse.wheelDelta > 0 ? -1 : 1);
					return 0;
				}
				auto& scroll = point.x < splitter_x() || mode_ == ItemMode::single ? imageScroll_ : itemsScroll_;
				if (scroll.scroll_by(-mouse.wheelDelta / 120 * 72))
				{
					rebuild_layout();
					queue_thumbnails();
					invalidate();
				}
				return 0;
			}
		case InputEvent::keyDown:
			{
				const bool control = key.control;
				const bool shift = key.shift;
				const bool activeFit = active_view().zoom.is_effectively_fit(active_view().bounds);
				const recti activeBounds = active_view().bounds;
				if (quickZoom_ && key.key == platform::KeyCode::space)
				{
					commit_quick_zoom();
					if (frame_) frame_->release_capture();
					return 0;
				}
				if (fullscreen_ && (control || activeFit) &&
					(key.key == platform::KeyCode::left || key.key == platform::KeyCode::right))
				{
					step_fullscreen_selection(key.key == platform::KeyCode::left ? -1 : 1, control);
					return 0;
				}
				if (control && key.character == L'1')
				{
					const pointi center{
						activeBounds.x + activeBounds.width / 2,
						activeBounds.y + activeBounds.height / 2
					};
					set_zoom(1.0, center);
					active_view().zoom.set_center(0.5, 0.5);
					invalidate();
					return 0;
				}
				if (control && key.character == L'0')
				{
					if (active_view().zoom.is_fit())
						set_zoom(active_view().zoom.explicit_scale(),
						         {activeBounds.x + activeBounds.width / 2,
						          activeBounds.y + activeBounds.height / 2});
					else set_fit(true);
					return 0;
				}
				if (control && (key.key == platform::KeyCode::add || key.key == platform::KeyCode::subtract))
				{
					zoom(key.key == platform::KeyCode::add ? 1.25 : 0.8);
					return 0;
				}
				if (!activeFit && (key.key == platform::KeyCode::left || key.key == platform::KeyCode::right ||
					key.key == platform::KeyCode::up || key.key == platform::KeyCode::down))
				{
					pan_by(key.key == platform::KeyCode::left
						       ? metric(48)
						       : key.key == platform::KeyCode::right
						       ? -metric(48)
						       : 0,
					       key.key == platform::KeyCode::up
						       ? metric(48)
						       : key.key == platform::KeyCode::down
						       ? -metric(48)
						       : 0);
					return 0;
				}
				if (!activeFit && (key.key == platform::KeyCode::pageUp || key.key == platform::KeyCode::pageDown))
				{
					pan_by(0, key.key == platform::KeyCode::pageUp ? activeBounds.height : -activeBounds.height);
					return 0;
				}
				if (!activeFit && (key.key == platform::KeyCode::home || key.key == platform::KeyCode::end))
				{
					const double edge = key.key == platform::KeyCode::home ? 0.0 : 1.0;
					active_view().zoom.set_center(edge, edge);
					invalidate();
					return 0;
				}
				if (control && key.key == platform::KeyCode::space && active_selection().focus() >= 0)
				{
					active_selection().click(active_selection().focus(), true, false);
					notify_selection();
					invalidate();
					return 0;
				}
				const int focused = active_selection().focus();
				if (!control && key.key == platform::KeyCode::enter && focused >= 0 &&
					static_cast<size_t>(focused) < items_.size() && media::is_media(items_[focused].kind) &&
					activationHandler_)
				{
					activationHandler_(files_[focused]);
					return 0;
				}
				if (key.key == platform::KeyCode::enter && commandHandlers_.enterFullscreen)
				{
					commandHandlers_.enterFullscreen();
					return 0;
				}
				if (key.key == platform::KeyCode::escape)
				{
					if (cancel_transient_state()) return 0;
					if (!active_view().zoom.is_fit()) set_fit(true);
					else if (commandHandlers_.leaveFullscreen) commandHandlers_.leaveFullscreen();
					return 0;
				}
				int next = -1;
				if (key.key == platform::KeyCode::left) next = spatial_neighbor(active_selection().focus(), -1, 0);
				if (key.key == platform::KeyCode::right) next = spatial_neighbor(active_selection().focus(), 1, 0);
				if (key.key == platform::KeyCode::up) next = spatial_neighbor(active_selection().focus(), 0, -1);
				if (key.key == platform::KeyCode::down) next = spatial_neighbor(active_selection().focus(), 0, 1);
				if (key.key == platform::KeyCode::home && !files_.empty()) next = 0;
				if (key.key == platform::KeyCode::end && !files_.empty()) next = static_cast<int>(files_.size()) - 1;
				if (key.key == platform::KeyCode::pageUp) next = spatial_neighbor(active_selection().focus(), 0, -2);
				if (key.key == platform::KeyCode::pageDown) next = spatial_neighbor(active_selection().focus(), 0, 2);
				if (key.key == platform::KeyCode::space)
					next = active_selection().focus() + 1 < static_cast<int>(files_.size())
						       ? active_selection().focus() + 1
						       : (files_.empty() ? -1 : 0);
				if (next >= 0)
				{
					active_selection().navigate(next, control, shift);
					ensure_visible(next);
					notify_selection();
					if (items_[next].kind != files::ItemKind::folder &&
						!media::is_media(items_[next].kind) && activationHandler_)
						activationHandler_(files_[next]);
					else load(files_[next]);
					invalidate();
					return 0;
				}
				break;
			}
		case InputEvent::contextMenu:
			if (suppressContextMenu_)
			{
				suppressContextMenu_ = false;
				return 0;
			}
			if (contextMenuHandler_)
				contextMenuHandler_(mouse.screenPoint);
			return 0;
		}
		return 0;
	}

	void ImageCanvas::paint(const platform::WindowFramePtr& frame, platform::DrawContext& draw)
	{
		frame_ = frame;
		const recti client = frame->client_rect();
		const int clientWidth = client.width;
		const int clientHeight = client.height;
		rebuild_layout();
		const ui::CanvasRenderer renderer(draw, textFont_);
		const color panelColor = platform::system_color(platform::SystemColor::window);
		renderer.fill({0, 0, clientWidth, clientHeight}, panelColor);
		renderer.clip(immersive_zoom() ? imagePane_ : imageScroll_.viewport(imagePane_));
		const bool showPrimary = !fullscreen_ || !immersive_zoom() || !activeComparison_;
		const bool showCollage = collage_active() && !collage_.empty();
		if (showPrimary && showCollage) paint_collage(renderer);
		else if (showPrimary && displayed_image())
		{
			const auto* image = displayed_image();
			double scale = quickZoom_ && !activeComparison_
				               ? quickZoomScale_
				               : primaryView_.zoom.effective_scale(primaryView_.bounds);
			if (primaryView_.zoom.is_fit() && !(quickZoom_ && !activeComparison_))
				scale = fit_scale();
			const recti destination = image_destination(scale, false);
			const bool alpha = displayedKind_ != files::ItemKind::image &&
				typeIconAlpha_[static_cast<size_t>(displayedKind_)];
			renderer.image({image->width, image->height, image->pixels, alpha, imageGeneration_}, destination);
		}
		else if (showPrimary)
		{
			if (primaryView_.sourceWidth > 0 && primaryView_.sourceHeight > 0)
			{
				const recti placeholder = primaryView_.zoom.destination(primaryView_.bounds);
				renderer.fill(placeholder, platform::system_color(platform::SystemColor::face));
				renderer.outline(placeholder, platform::system_color(platform::SystemColor::shadow));
			}
			const std::wstring_view message = primaryLoading_.failure() == ui::LoadingModel::Failure::unreadable
				                                  ? L"This image could not be read"
				                                  : path_.empty() ? L"Select an image to view" : L"Loading image...";
			renderer.text(message, primaryView_.bounds,
			              platform::system_color(platform::SystemColor::windowText),
			              platform::TextFormat::center | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine);
		}
		const bool showComparison = fullscreen_ && (!immersive_zoom() || activeComparison_);
		if (showComparison && !comparisonImage_.pixels.empty())
		{
			double scale = quickZoom_ && activeComparison_
				               ? quickZoomScale_
				               : comparisonView_.zoom.effective_scale(comparisonView_.bounds);
			if (comparisonView_.zoom.is_fit() && !(quickZoom_ && activeComparison_)) scale = fit_scale(true);
			const recti destination = image_destination(scale, true);
			renderer.image({comparisonImage_.width, comparisonImage_.height, comparisonImage_.pixels,
			                false, comparisonGeneration_},
			               destination);
		}
		else if (showComparison && comparisonLoading_.failure() == ui::LoadingModel::Failure::unreadable)
			renderer.text(L"This image could not be read", comparisonView_.bounds,
			              platform::system_color(platform::SystemColor::windowText),
			              platform::TextFormat::center | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine);
		if (fullscreen_) renderer.reset_clip();
		if (!immersive_zoom()) paint_properties(renderer);
		if (!fullscreen_) renderer.reset_clip();
		if (!immersive_zoom()) renderer.scrollbar(imageScroll_);
		if (!immersive_zoom() && (fullscreen_ || mode_ != ItemMode::single))
		{
			renderer.fill(splitterBounds_, platform::calc_hande_color(
				hoveringSplitter_, draggingSplitter_, false));
			paint_items(renderer);
			if (fullscreen_) renderer.scrollbar(thumbnailScroll_);
			else renderer.scrollbar(itemsScroll_);
			if (draggingMarquee_)
			{
				renderer.outline({
					                 (std::min)(marqueeStart_.x, marqueeCurrent_.x),
					                 (std::min)(marqueeStart_.y, marqueeCurrent_.y),
					                 std::abs(marqueeCurrent_.x - marqueeStart_.x),
					                 std::abs(marqueeCurrent_.y - marqueeStart_.y)
				                 }, platform::system_color(platform::SystemColor::highlight));
			}
		}
		if (selectingZoom_)
			renderer.outline({
				                 (std::min)(zoomSelectionStart_.x, zoomSelectionCurrent_.x),
				                 (std::min)(zoomSelectionStart_.y, zoomSelectionCurrent_.y),
				                 std::abs(zoomSelectionCurrent_.x - zoomSelectionStart_.x),
				                 std::abs(zoomSelectionCurrent_.y - zoomSelectionStart_.y)
			                 }, platform::system_color(platform::SystemColor::highlight));

		const auto& feedbackView = active_view();
		if (showCollage || feedbackView.bounds.is_empty() || feedbackView.sourceWidth <= 0 ||
			feedbackView.sourceHeight <= 0)
			return;
		const bool temporary = quickZoom_;
		const double feedbackScale = effective_scale(activeComparison_);
		const std::wstring zoomText = !temporary && feedbackView.zoom.is_fit()
			                              ? L"Fit"
			                              : std::abs(feedbackScale - 1.0) < 0.0005
			                              ? L"100% (1:1)"
			                              : std::format(L"{}%", static_cast<int>(std::lround(feedbackScale * 100.0)));
		const sizei zoomTextSize = renderer.measure_text(zoomText);
		const int feedbackPadding = metric(7);
		recti zoomBadge{
			feedbackView.bounds.right() - zoomTextSize.width - feedbackPadding * 2 - metric(8),
			feedbackView.bounds.y + metric(8), zoomTextSize.width + feedbackPadding * 2,
			(std::max)(metric(24), zoomTextSize.height + feedbackPadding)
		};
		renderer.blend_fill(zoomBadge, temporary ? color{255, 244, 190, 220} : color{245, 245, 245, 220});
		renderer.outline(zoomBadge, platform::system_color(platform::SystemColor::shadow));
		renderer.text(zoomText, zoomBadge, colors::black,
		              platform::TextFormat::center | platform::TextFormat::verticalCenter |
		              platform::TextFormat::singleLine);
		const bool provisional = activeComparison_ ? comparisonLoading_.provisional() : primaryLoading_.provisional();
		if (provisional)
		{
			const recti qualityBadge{zoomBadge.x, zoomBadge.bottom() + metric(4), zoomBadge.width, metric(22)};
			renderer.blend_fill(qualityBadge, color{245, 245, 245, 220});
			renderer.text(L"Improving...", qualityBadge, colors::black,
			              platform::TextFormat::center | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine);
		}
	}

	void ImageCanvas::rebuild_layout()
	{
		const recti client = frame_ ? frame_->client_rect() : recti{};
		const int width = (std::max)(0, client.width);
		const int height = (std::max)(0, client.height);
		rootElement_ = {};
		rootElement_.style.axis = fullscreen_ && !immersive_zoom() ? ui::Axis::column : ui::Axis::row;
		propertyToggleBounds_ = {};
		comparisonPropertyToggleBounds_ = {};
		propertyHeaderBounds_ = {};
		comparisonPropertyHeaderBounds_ = {};
		stripHeaderBounds_ = {};
		stripToggleBounds_ = {};

		ui::Element image;
		image.id = -10;
		if (fullscreen_ && !immersive_zoom())
		{
			image.style.grow = 1;
			image.style.minimum = metric(180);
			ui::Element items;
			items.id = -12;
			items.style.basis = thumbnailsExpanded_ ? metric(thumbnailSize_ + 90) : metric(32);
			rootElement_.children.push_back(std::move(image));
			rootElement_.children.push_back(std::move(items));
		}
		else if (mode_ == ItemMode::single || immersive_zoom())
		{
			image.style.grow = 1;
			rootElement_.children.push_back(std::move(image));
		}
		else
		{
			const int minimumPane = metric(180);
			const int split = std::clamp(splitterPosition_ < 0 ? width * 3 / 5 : metric(splitterPosition_), minimumPane,
			                             (std::max)(minimumPane, width - minimumPane));
			image.style.basis = (std::max)(0, split - metric(3));
			image.style.minimum = metric(120);
			ui::Element splitter;
			splitter.id = -11;
			splitter.style.basis = metric(6);
			ui::Element items;
			items.id = -12;
			items.style.grow = 1;
			items.style.minimum = metric(120);
			rootElement_.children.push_back(std::move(image));
			rootElement_.children.push_back(std::move(splitter));
			rootElement_.children.push_back(std::move(items));
		}
		ui::FlexBox::arrange(rootElement_, {0, 0, width, height});
		imagePane_ = rootElement_.children.front().bounds;
		if (fullscreen_ && !immersive_zoom())
		{
			splitterBounds_ = {};
			itemsViewport_ = rootElement_.children[1].bounds;
		}
		else if (mode_ == ItemMode::single || immersive_zoom())
		{
			splitterBounds_ = {};
			itemsViewport_ = {};
			itemFlow_ = {};
			contentHeight_ = 0;
		}
		else
		{
			splitterBounds_ = rootElement_.children[1].bounds;
			itemsViewport_ = rootElement_.children[2].bounds;
		}
		detailsHeaderBounds_ = {};
		if (mode_ == ItemMode::details)
		{
			const int headerHeight = metric(32);
			detailsHeaderBounds_ = {itemsViewport_.x, itemsViewport_.y, itemsViewport_.width, headerHeight};
			itemsViewport_.y += headerHeight;
			itemsViewport_.height = (std::max)(0, itemsViewport_.height - headerHeight);
		}

		if (immersive_zoom())
		{
			imageScroll_.layout(imagePane_, imagePane_.height, 0, metric(12), metric(28));
			if (fullscreen_ && activeComparison_)
			{
				primaryView_.bounds = {};
				comparisonView_.bounds = imagePane_;
			}
			else
			{
				primaryView_.bounds = imagePane_;
				comparisonView_.bounds = {};
			}
			propertyFlow_ = {};
			thumbnailScroll_ = {};
			return;
		}

		if (fullscreen_)
		{
			imageScroll_.layout(imagePane_, imagePane_.height, 0, metric(12), metric(28));
			const bool comparing = fullscreenSelection_.secondary() >= 0;
			const int halfWidth = comparing ? imagePane_.width / 2 : imagePane_.width;
			primaryView_.bounds = {
				imagePane_.x, imagePane_.y, halfWidth, (std::max)(0, imagePane_.height - metric(8))
			};
			comparisonView_.bounds = comparing
				                         ? recti{
					                         imagePane_.x + halfWidth, imagePane_.y,
					                         (std::max)(0, imagePane_.width - halfWidth),
					                         (std::max)(0, imagePane_.height - metric(8))
				                         }
				                         : recti{};
			propertyFlow_ = {};
			stripHeaderBounds_ = {itemsViewport_.x, itemsViewport_.y, itemsViewport_.width, metric(32)};
			stripToggleBounds_ = {
				stripHeaderBounds_.right() - metric(32), stripHeaderBounds_.y, metric(32), metric(32)
			};
			if (!thumbnailsExpanded_)
			{
				itemsViewport_ = {};
				itemFlow_ = {};
				thumbnailScroll_ = {};
				return;
			}
			itemsViewport_.y += stripHeaderBounds_.height;
			itemsViewport_.height = (std::max)(0, itemsViewport_.height - stripHeaderBounds_.height);
			itemFlow_ = {};
			itemFlow_.bounds = itemsViewport_;
			const int tileWidth = metric(thumbnailSize_ + 16);
			const int tileHeight = metric(thumbnailSize_ + 38);
			const int imageCount = static_cast<int>(std::ranges::count(items_, files::ItemKind::image,
			                                                           &files::FolderItem::kind));
			stripContentWidth_ = metric(8) + imageCount * (tileWidth + metric(8));
			thumbnailScroll_.layout(itemsViewport_, stripContentWidth_, stripOffset_, metric(12), metric(28));
			stripOffset_ = thumbnailScroll_.offset;
			int x = itemsViewport_.x + metric(8) - thumbnailScroll_.offset;
			for (int index = 0; index < static_cast<int>(items_.size()); ++index)
			{
				if (items_[index].kind != files::ItemKind::image) continue;
				ui::Element item;
				item.id = index;
				item.bounds = {x, itemsViewport_.y + metric(8), tileWidth, tileHeight};
				itemFlow_.children.push_back(std::move(item));
				x += tileWidth + metric(8);
			}
			contentHeight_ = tileHeight + metric(16);
			itemsScroll_.layout(itemsViewport_, itemsViewport_.height, 0, metric(12), metric(28));
			return;
		}

		const int propertyColumns = (std::max)(1, imagePane_.width / metric(260));
		const int propertyRows = (static_cast<int>(properties_.size()) + propertyColumns - 1) / propertyColumns;
		const int propertyListHeight = properties_.empty() || !propertiesExpanded_
			                               ? 0
			                               : metric(8) + propertyRows * metric(26) +
			                               (std::max)(0, propertyRows - 1) * metric(2);
		const int propertiesHeight = properties_.empty() ? 0 : metric(34) + propertyListHeight;
		const recti proposedFitBounds{
			imagePane_.x + metric(12), imagePane_.y + metric(12),
			(std::max)(0, imagePane_.width - metric(24)),
			(std::max)(0, imagePane_.height - propertiesHeight - metric(24))
		};
		const int imageSectionHeight = primaryView_.zoom.is_effectively_fit(proposedFitBounds)
			                               ? (std::max)(metric(220), imagePane_.height - propertiesHeight)
			                               : (std::max)(metric(220),
		                                            static_cast<int>(primaryView_.sourceHeight *
		                                                                 primaryView_.zoom.effective_scale(
			                                                                 primaryView_.bounds))
			                                            +
			                                            metric(32));
		imageContentHeight_ = imageSectionHeight + propertiesHeight;
		imageScroll_.layout(imagePane_, imageContentHeight_, imageScroll_.offset, metric(12), metric(28));
		const recti imageViewport = imageScroll_.viewport(imagePane_);
		primaryView_.bounds = {
			imageViewport.x + metric(12), imagePane_.y - imageScroll_.offset + metric(12),
			(std::max)(0, imageViewport.width - metric(24)), (std::max)(0, imageSectionHeight - metric(24))
		};
		arrange_collage();
		propertyFlow_ = {};
		propertyFlow_.style.padding = metric(12);
		propertyFlow_.style.gap = metric(2);
		for (int index = 0; index < static_cast<int>(properties_.size()); ++index)
		{
			ui::Element row;
			row.id = index;
			propertyFlow_.children.push_back(std::move(row));
		}
		ui::FlexBox::arrange_list(propertyFlow_,
		                          {
			                          imageViewport.x,
			                          imagePane_.y - imageScroll_.offset + imageSectionHeight + metric(34),
			                          imageViewport.width, propertyListHeight
		                          }, metric(26));
		propertyToggleBounds_ = {
			imageViewport.right() - metric(34),
			imagePane_.y - imageScroll_.offset + imageSectionHeight, metric(34), metric(34)
		};

		if (mode_ == ItemMode::single) return;

		itemFlow_ = {};
		itemFlow_.style.axis = ui::Axis::row;
		itemFlow_.style.wrap = true;
		itemFlow_.style.gap = metric(8);
		itemFlow_.style.padding = metric(8);
		itemFlow_.children.reserve(files_.size());
		for (int index = 0; index < static_cast<int>(files_.size()); ++index)
		{
			ui::Element item;
			item.id = index;
			if (mode_ == ItemMode::details)
			{
				item.style.basis = (std::max)(1, itemsViewport_.width - metric(16));
				item.intrinsic = {item.style.basis, metric(40)};
			}
			else if (mode_ == ItemMode::matrix)
			{
				item.style.basis = metric(thumbnailSize_ + 16);
				item.style.minimum = metric(80);
				item.intrinsic = {item.style.basis, metric(thumbnailSize_ + 38)};
			}
			else
			{
				const int imageHeight = metric(thumbnailSize_);
				const auto& thumbnail = thumbnails_[index];
				const double ratio = thumbnail.height > 0
					                     ? static_cast<double>(thumbnail.width) / thumbnail.height
					                     : 4.0 / 3.0;
				const int imageWidth = std::clamp(static_cast<int>(imageHeight * ratio), metric(64),
				                                  metric(thumbnailSize_));
				item.style.basis = imageWidth + metric(16);
				item.style.minimum = metric(80);
				item.intrinsic = {item.style.basis, imageHeight + metric(38)};
			}
			itemFlow_.children.push_back(std::move(item));
		}

		const auto arrangeItems = [this](const recti viewport)
		{
			if (mode_ == ItemMode::details)
			{
				for (auto& item : itemFlow_.children)
					item.style.basis = (std::max)(1, viewport.width - itemFlow_.style.padding * 2);
			}
			else if (mode_ == ItemMode::matrix)
			{
				const int innerWidth = (std::max)(1, viewport.width - itemFlow_.style.padding * 2);
				const int desiredWidth = metric(thumbnailSize_ + 16);
				const int columns = (std::max)(1, (innerWidth + itemFlow_.style.gap) /
				                               (desiredWidth + itemFlow_.style.gap));
				const int cellWidth = (std::max)(1, (innerWidth - (columns - 1) * itemFlow_.style.gap) / columns);
				for (auto& item : itemFlow_.children) item.style.basis = cellWidth;
			}
			const auto size = ui::FlexBox::arrange(itemFlow_, {
				                                       viewport.x, viewport.y - itemsScroll_.offset,
				                                       viewport.width, viewport.height + itemsScroll_.offset
			                                       });
			if (mode_ == ItemMode::thumbnails)
			{
				const int left = viewport.x + itemFlow_.style.padding;
				const int innerWidth = (std::max)(0, viewport.width - itemFlow_.style.padding * 2);
				for (size_t first = 0; first < itemFlow_.children.size();)
				{
					size_t last = first + 1;
					int itemWidths = itemFlow_.children[first].bounds.width;
					while (last < itemFlow_.children.size() &&
						itemFlow_.children[last].bounds.y == itemFlow_.children[first].bounds.y)
						itemWidths += itemFlow_.children[last++].bounds.width;
					const int spacing = (std::max)(0, innerWidth - itemWidths) / static_cast<int>(last - first + 1);
					int x = left + spacing;
					for (size_t index = first; index < last; ++index)
					{
						itemFlow_.children[index].bounds.x = x;
						x += itemFlow_.children[index].bounds.width + spacing;
					}
					first = last;
				}
			}
			return size;
		};
		contentHeight_ = arrangeItems(itemsViewport_).height;
		itemsScroll_.layout(itemsViewport_, contentHeight_, itemsScroll_.offset);
		itemsViewport_ = itemsScroll_.viewport(itemsViewport_);
		if (mode_ == ItemMode::details) detailsHeaderBounds_.width = itemsViewport_.width;
		contentHeight_ = arrangeItems(itemsViewport_).height;
		itemsScroll_.layout({
			                    itemsViewport_.x, itemsViewport_.y,
			                    itemsViewport_.width + (itemsScroll_.visible() ? itemsScroll_.track.width : 0),
			                    itemsViewport_.height
		                    },
		                    contentHeight_, itemsScroll_.offset);
	}

	int ImageCanvas::splitter_x() const { return splitterBounds_.x + splitterBounds_.width / 2; }

	recti ImageCanvas::item_rect(const int index) const
	{
		if (fullscreen_)
		{
			const auto item = std::ranges::find(itemFlow_.children, index, &ui::Element::id);
			return item == itemFlow_.children.end() ? recti{} : item->bounds;
		}
		return index >= 0 && index < static_cast<int>(itemFlow_.children.size())
			       ? itemFlow_.children[index].bounds
			       : recti{};
	}

	void ImageCanvas::invalidate_item(const int index) const
	{
		const recti bounds = item_rect(index);
		if (frame_ && !bounds.is_empty()) frame_->invalidate(bounds);
	}

	void ImageCanvas::invalidate_image_pane() const
	{
		if (frame_ && !primaryView_.bounds.is_empty()) frame_->invalidate(primaryView_.bounds);
	}

	int ImageCanvas::hit_test_item(const pointi point) const
	{
		if ((!fullscreen_ && mode_ == ItemMode::single) || !itemsViewport_.contains(point)) return -1;
		const auto* hit = ui::FlexBox::hit_test(itemFlow_, point);
		return hit && hit->id >= 0 ? hit->id : -1;
	}

	void ImageCanvas::select_index(const int index, const bool extend, const bool toggle)
	{
		active_selection().click(index, toggle, extend);
		ensure_visible(index);
		invalidate();
		notify_selection();
	}

	void ImageCanvas::select_all()
	{
		active_selection().select_all();
		invalidate();
		notify_selection();
	}

	void ImageCanvas::clear_selection()
	{
		active_selection().clear();
		invalidate();
		notify_selection();
	}

	std::vector<std::filesystem::path> ImageCanvas::selected_paths() const
	{
		std::vector<std::filesystem::path> result;
		result.reserve(selection_.size());
		for (const int index : selection_.selected())
			if (index >= 0 && index < static_cast<int>(files_.size())) result.push_back(files_[index]);
		return result;
	}

	void ImageCanvas::ensure_visible(const int index)
	{
		const recti item = item_rect(index);
		if (fullscreen_)
		{
			if (item.x < itemsViewport_.x) stripOffset_ -= itemsViewport_.x - item.x;
			else if (item.right() > itemsViewport_.right()) stripOffset_ += item.right() - itemsViewport_.right();
			stripOffset_ = std::clamp(stripOffset_, 0, (std::max)(0, stripContentWidth_ - itemsViewport_.width));
			rebuild_layout();
			queue_thumbnails();
			return;
		}
		if (item.y < itemsViewport_.y) itemsScroll_.offset -= itemsViewport_.y - item.y;
		else if (item.bottom() > itemsViewport_.bottom())
			itemsScroll_.offset += item.bottom() - itemsViewport_.bottom();
		itemsScroll_.offset = std::clamp(itemsScroll_.offset, 0, itemsScroll_.maximum());
		rebuild_layout();
		queue_thumbnails();
	}

	void ImageCanvas::step_focus(const int direction)
	{
		if (files_.empty()) return;
		const int index = active_selection().move_focus(direction);
		ensure_visible(index);
		notify_selection();
		if (items_[index].kind != files::ItemKind::folder &&
			!media::is_media(items_[index].kind) && activationHandler_)
			activationHandler_(files_[index]);
		else load(files_[index]);
	}

	void ImageCanvas::notify_selection()
	{
		rebuild_properties();
		rebuild_collage();
		imageScroll_.offset = 0;
		rebuild_layout();
		if (selectionHandler_) selectionHandler_();
	}

	int ImageCanvas::spatial_neighbor(const int index, const int horizontal, const int vertical) const
	{
		if (itemFlow_.children.empty()) return -1;
		if (index < 0 || index >= static_cast<int>(itemFlow_.children.size())) return 0;
		if (horizontal)
		{
			const int count = static_cast<int>(itemFlow_.children.size());
			return (index + (horizontal < 0 ? -1 : 1) + count) % count;
		}
		const auto current = item_rect(index);
		const int centerX = current.x + current.width / 2;
		const int centerY = current.y + current.height / 2;
		if (std::abs(vertical) == 2)
		{
			const int targetY = centerY + (vertical < 0 ? -itemsViewport_.height : itemsViewport_.height);
			int best = index;
			int score = INT_MAX;
			for (const auto& candidate : itemFlow_.children)
			{
				const int candidateX = candidate.bounds.x + candidate.bounds.width / 2;
				const int candidateY = candidate.bounds.y + candidate.bounds.height / 2;
				const int value = std::abs(candidateY - targetY) * 4 + std::abs(candidateX - centerX);
				if (value < score)
				{
					score = value;
					best = candidate.id;
				}
			}
			return best;
		}
		int targetRow = vertical < 0 ? INT_MIN : INT_MAX;
		for (const auto& candidate : itemFlow_.children)
		{
			if (candidate.id == index) continue;
			const int row = candidate.bounds.y;
			if (vertical < 0 && row < current.y) targetRow = (std::max)(targetRow, row);
			if (vertical > 0 && row > current.y) targetRow = (std::min)(targetRow, row);
		}
		if (targetRow == INT_MIN || targetRow == INT_MAX) return index;
		int best = index;
		std::int64_t distance = (std::numeric_limits<std::int64_t>::max)();
		for (const auto& candidate : itemFlow_.children)
		{
			if (candidate.bounds.y != targetRow) continue;
			const int candidateX = candidate.bounds.x + candidate.bounds.width / 2;
			const int candidateY = candidate.bounds.y + candidate.bounds.height / 2;
			const auto deltaX = static_cast<std::int64_t>(candidateX) - centerX;
			const auto deltaY = static_cast<std::int64_t>(candidateY) - centerY;
			const auto candidateDistance = deltaX * deltaX + deltaY * deltaY;
			if (candidateDistance < distance)
			{
				distance = candidateDistance;
				best = candidate.id;
			}
		}
		return best;
	}

	void ImageCanvas::rebuild_properties()
	{
		properties_.clear();
		const auto& selected = active_selection();
		if (selected.size() == 0 && !files_.empty())
		{
			queue_metadata({}, false, 0);
			properties_.push_back({L"Selected", L"No images"});
			if (!path_.empty()) properties_.push_back({L"Displayed", path_.filename().wstring()});
			return;
		}
		if (selected.size() > 1)
		{
			constexpr size_t metadataLimit = 64;
			std::map<std::wstring, size_t> types;
			std::vector<std::filesystem::path> metadataPaths;
			metadataPaths.reserve((std::min)(metadataLimit, selected.size()));
			for (const int index : selected.selected())
			{
				if (index < 0 || index >= static_cast<int>(files_.size())) continue;
				auto extension = files_[index].extension().wstring();
				std::ranges::transform(extension, extension.begin(), towupper);
				++types[extension.empty() ? L"Other" : extension.substr(1)];
				if (metadataPaths.size() < metadataLimit) metadataPaths.push_back(files_[index]);
			}
			std::wstring typeSummary;
			for (const auto& [type, count] : types)
			{
				if (!typeSummary.empty()) typeSummary += L", ";
				typeSummary += std::format(L"{} {}", count, type);
			}
			const bool imagesOnly = std::ranges::all_of(selected.selected(), [this](const int index)
			{
				return index >= 0 && index < static_cast<int>(items_.size()) &&
					items_[index].kind == files::ItemKind::image;
			});
			properties_.push_back({
				L"Selected", std::format(L"{} {}", selected.size(),
				                         imagesOnly ? L"images" : L"items")
			});
			properties_.push_back({L"Formats", std::move(typeSummary)});
			properties_.push_back({
				metadataPaths.size() < selected.size() ? L"Sample size" : L"Total size",
				L"Loading..."
			});
			if (!imagesOnly)
				properties_.push_back({L"Folder", files_.empty() ? L"" : files_.front().parent_path().wstring()});
			if (selected.focus() >= 0 && selected.focus() < static_cast<int>(files_.size()))
				properties_.push_back({L"Focused", files_[selected.focus()].filename().wstring()});
			queue_metadata(std::move(metadataPaths), true, selected.size());
			return;
		}
		if (path_.empty())
		{
			queue_metadata({}, false, 0);
			return;
		}
		const auto item = std::ranges::find(items_, path_, &files::FolderItem::path);
		const files::ItemKind kind = item == items_.end() ? displayedKind_ : item->kind;
		properties_.push_back({
			L"Type", item == items_.end() ? format::file_type(path_) : format::item_type(*item)
		});
		if (displayedKind_ == files::ItemKind::image)
			properties_.push_back({
				L"Dimensions",
				std::format(L"{} x {} pixels", primaryView_.sourceWidth, primaryView_.sourceHeight)
			});
		if (kind != files::ItemKind::folder) properties_.push_back({L"File size", L"Loading..."});
		properties_.push_back({L"Modified", L"Loading..."});
		if (kind != files::ItemKind::image)
			properties_.push_back({
				kind == files::ItemKind::folder ? L"Location" : L"Folder",
				path_.parent_path().wstring()
			});
		queue_metadata({path_}, false, 1);
	}

	void ImageCanvas::queue_metadata(std::vector<std::filesystem::path> paths, const bool aggregate,
	                                 const size_t selectionCount)
	{
		const size_t total = paths.size();
		MetadataJob job{++metadataGeneration_, std::move(paths), aggregate, selectionCount};
		job.values.reserve(total);
		if (total == 0)
		{
			if (informationProgressHandler_) informationProgressHandler_(0, 0);
			return;
		}
		pump_metadata(std::move(job));
	}

	void ImageCanvas::apply_metadata(const MetadataJob& result)
	{
		for (size_t index = 0; index < result.paths.size() && index < result.values.size(); ++index)
			metadataCache_[result.paths[index]] = result.values[index];
		if (result.aggregate)
		{
			std::uintmax_t totalBytes = 0;
			for (const auto& metadata : result.values)
				if (metadata.hasSize) totalBytes += metadata.size;
			for (auto& property : properties_)
				if (property.name == L"Sample size" || property.name == L"Total size")
				{
					property.value = result.paths.size() < result.selectionCount
						                 ? std::format(L"{} across first {} files", format::size(totalBytes),
						                               result.paths.size())
						                 : format::size(totalBytes);
					break;
				}
			return;
		}
		if (result.values.empty()) return;
		const auto& metadata = result.values.front();
		for (auto& property : properties_)
		{
			if (property.name == L"File size")
				property.value = metadata.hasSize
					                 ? format::size(metadata.size)
					                 : L"Unavailable";
			else if (property.name == L"Modified")
				property.value = metadata.hasModified ? format::modified(metadata.modified) : L"Unknown";
		}
		if (!metadata.dateTaken.empty()) properties_.push_back({L"Captured", metadata.dateTaken});
		if (!metadata.camera.empty()) properties_.push_back({L"Camera", metadata.camera});
		if (metadata.hasDuration) properties_.push_back({L"Duration", media::duration_text(metadata.duration)});
		if (metadata.videoWidth > 0 && metadata.videoHeight > 0)
			properties_.push_back({L"Video size", format::dimensions(metadata.videoWidth, metadata.videoHeight)});
		if (metadata.frameRate > 0)
			properties_.push_back({L"Frame rate", std::format(L"{:.3f} fps", metadata.frameRate)});
		if (!metadata.codec.empty()) properties_.push_back({L"Codec", metadata.codec});
		if (!metadata.mediaError.empty()) properties_.push_back({L"Media", metadata.mediaError});
		if (media::is_media(displayedKind_))
			properties_.push_back({L"Playback", L"Double-click the item or press Enter to play."});
		std::wstring exposure;
		for (const auto& value : {metadata.exposure, metadata.aperture, metadata.iso, metadata.focalLength})
			if (!value.empty()) exposure += (exposure.empty() ? L"" : L"  ") + value;
		if (!exposure.empty()) properties_.push_back({L"Exposure", std::move(exposure)});
	}

	void ImageCanvas::paint_properties(const ui::CanvasRenderer& renderer)
	{
		if (properties_.empty()) return;
		const auto glyph = [](const std::wstring_view name)
		{
			if (name == L"Type" || name == L"Formats") return L"\x25A3";
			if (name == L"Dimensions") return L"\x25A7";
			if (name == L"Folder" || name == L"Location") return L"\x2302";
			if (name == L"Modified" || name == L"Captured") return L"\x25F7";
			if (name == L"Camera") return L"\x25C9";
			return L"\x25C7";
		};
		const auto paintPanel = [&](const std::wstring& title, const std::vector<Property>& rows,
		                            const recti available, const bool rightAligned, const bool expanded,
			                            recti& toggle, recti& header, const std::uint8_t opacity,
			                            const bool fullWidth = false)
		{
			const int headerHeight = metric(34);
			const int rowHeight = metric(24);
			int contentWidth = renderer.measure_text(title).width + metric(56);
			for (const auto& row : rows)
			{
				const int width = renderer.measure_text(row.name).width + renderer.measure_text(row.value).width +
					metric(64);
				contentWidth = (std::max)(contentWidth, width);
			}
			const int minimumWidth = metric(150);
			const int panelWidth = fullWidth ? available.width :
				std::clamp(contentWidth, minimumWidth, (std::max)(minimumWidth, available.width));
			const int columns = fullWidth ? (std::max)(1, panelWidth / metric(260)) : 1;
			const int rowCount = (static_cast<int>(rows.size()) + columns - 1) / columns;
			const int panelHeight = headerHeight + (expanded ? rowCount * rowHeight + metric(6) : 0);
			const int x = rightAligned ? available.right() - panelWidth : available.x;
			const recti panel{x, available.bottom() - panelHeight, panelWidth, panelHeight};
			header = {panel.x, panel.y, panel.width, headerHeight};
			renderer.blend_fill(panel, colors::light_gray.with_alpha(opacity));
			toggle = {panel.right() - headerHeight, panel.y, headerHeight, headerHeight};
			renderer.text(title, {panel.x + metric(12), panel.y + metric(4), panel.width - metric(52), metric(26)},
			              colors::black,
			              platform::TextFormat::left | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
			renderer.text(expanded ? L"\x25BC" : L"\x25B2", toggle, colors::black,
			              platform::TextFormat::center | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine);
			if (!expanded) return;
			const int cellWidth = panel.width / columns;
			for (size_t index = 0; index < rows.size(); ++index)
			{
				const auto& row = rows[index];
				const int column = static_cast<int>(index) % columns;
				const int line = static_cast<int>(index) / columns;
				const int cellX = panel.x + column * cellWidth;
				const int y = panel.y + headerHeight + line * rowHeight;
				const int labelWidth = renderer.measure_text(row.name).width;
				renderer.text(glyph(row.name), {cellX + metric(8), y, metric(22), rowHeight}, colors::black,
				              platform::TextFormat::center | platform::TextFormat::verticalCenter |
				              platform::TextFormat::singleLine);
				renderer.text(row.name, {cellX + metric(36), y, labelWidth, rowHeight}, colors::black,
				              platform::TextFormat::left | platform::TextFormat::verticalCenter |
				              platform::TextFormat::singleLine);
				renderer.text(row.value,
				              {cellX + metric(44) + labelWidth, y, cellWidth - labelWidth - metric(52), rowHeight},
				              colors::black,
				              platform::TextFormat::left | platform::TextFormat::verticalCenter |
				              platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
			}
		};
		if (fullscreen_)
		{
			const auto paintSummary = [&](const int index, const recti bounds)
			{
				if (index < 0 || index >= static_cast<int>(items_.size())) return;
				const auto& item = items_[index];
				std::vector<Property> rows{{L"Type", format::item_type(item)}};
				if (has_thumbnail(item.kind) && item.width > 0 && item.height > 0)
					rows.push_back({L"Dimensions", std::format(L"{} x {} pixels", item.width, item.height)});
				if (item.hasDuration) rows.push_back({L"Duration", media::duration_text(item.duration)});
				if (item.kind != files::ItemKind::folder && item.hasSize)
					rows.push_back({L"File size", format::size(item.size)});
				if (item.hasModified) rows.push_back({L"Modified", format::modified(item.modified)});
				if (const auto metadata = metadataCache_.find(item.path); metadata != metadataCache_.end())
				{
					if (!metadata->second.dateTaken.empty()) rows.push_back({L"Captured", metadata->second.dateTaken});
					if (!metadata->second.camera.empty()) rows.push_back({L"Camera", metadata->second.camera});
					std::wstring exposure;
					for (const auto& value : {
						     metadata->second.exposure, metadata->second.aperture,
						     metadata->second.iso, metadata->second.focalLength
					     })
						if (!value.empty()) exposure += (exposure.empty() ? L"" : L"  ") + value;
					if (!exposure.empty()) rows.push_back({L"Exposure", std::move(exposure)});
				}
				if (item.kind != files::ItemKind::image)
					rows.push_back({
						item.kind == files::ItemKind::folder ? L"Location" : L"Folder",
						item.path.parent_path().wstring()
					});
				const bool primary = index == fullscreenSelection_.focus();
				const bool expanded = primary ? propertiesExpanded_ : comparisonPropertiesExpanded_;
				recti& toggle = primary ? propertyToggleBounds_ : comparisonPropertyToggleBounds_;
				recti& header = primary ? propertyHeaderBounds_ : comparisonPropertyHeaderBounds_;
				const recti client = frame_ ? frame_->client_rect() : recti{};
				const recti available = !thumbnailsExpanded_
					? recti{primary ? 0 : client.width / 2, 0,
					        fullscreenSelection_.secondary() >= 0 ? client.width / 2 : client.width, client.height}
					: recti{bounds.x, bounds.y, bounds.width, bounds.height};
				paintPanel(item.path.filename().wstring(), rows, available, !primary, expanded, toggle, header, 210);
				if (!thumbnailsExpanded_)
				{
					const int padding = metric(8);
					if (primary)
					{
						const int left = (std::min)(stripHeaderBounds_.right(), header.right() + padding);
						stripHeaderBounds_.width = (std::max)(0, stripHeaderBounds_.right() - left);
						stripHeaderBounds_.x = left;
					}
					else
						stripHeaderBounds_.width = (std::max)(0,
							(std::max)(stripHeaderBounds_.x, header.x - padding) - stripHeaderBounds_.x);
					stripToggleBounds_ = {
						stripHeaderBounds_.right() - metric(32), stripHeaderBounds_.y, metric(32), metric(32)
					};
				}
			};
			paintSummary(fullscreenSelection_.focus(), primaryView_.bounds);
			if (fullscreenSelection_.secondary() >= 0)
				paintSummary(fullscreenSelection_.secondary(), comparisonView_.bounds);
			return;
		}
		const auto selectedCount = active_selection().size();
		const std::wstring title = selectedCount > 1
			                           ? std::format(L"{} items selected", selectedCount)
			                           : path_.empty()
			                           ? L"Properties"
			                           : path_.filename().wstring();
		const recti available{
			propertyFlow_.bounds.x, propertyFlow_.bounds.y - metric(34),
			propertyFlow_.bounds.width, propertyFlow_.bounds.height + metric(34)
		};
		paintPanel(title, properties_, available, false, propertiesExpanded_, propertyToggleBounds_,
		           propertyHeaderBounds_, 220, true);
	}

	void ImageCanvas::paint_items(const ui::CanvasRenderer& renderer)
	{
		if (fullscreen_)
		{
			renderer.blend_fill(stripHeaderBounds_, colors::light_gray.with_alpha(225));
			renderer.text(L"Images", {
				              stripHeaderBounds_.x + metric(12), stripHeaderBounds_.y,
				              stripHeaderBounds_.width - metric(52), stripHeaderBounds_.height
			              }, colors::black,
			              platform::TextFormat::left | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine);
			renderer.text(thumbnailsExpanded_ ? L"\x25BC" : L"\x25B2", stripToggleBounds_, colors::black,
			              platform::TextFormat::center | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine);
			if (!thumbnailsExpanded_) return;
		}
		if (mode_ == ItemMode::details) paint_details_header(renderer);
		renderer.fill(itemsViewport_, platform::system_color(platform::SystemColor::window));
		renderer.clip(itemsViewport_);
		if (files_.empty())
		{
			const std::wstring message = totalItemCount_ == 0
				                             ? L"empty folder"
				                             : std::format(L"Click to show {} filtered out items.", totalItemCount_);
			renderer.text(message, itemsViewport_, platform::system_color(platform::SystemColor::grayText),
			              platform::TextFormat::center | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine);
		}
		else mode_ == ItemMode::details ? paint_details(renderer) : paint_tiles(renderer);
		renderer.reset_clip();
	}

	void ImageCanvas::paint_details_header(const ui::CanvasRenderer& renderer)
	{
		renderer.fill(detailsHeaderBounds_, platform::system_color(platform::SystemColor::face));
		const auto columns = media_detail_columns(detailsHeaderBounds_);
		constexpr std::array labels{L"Name", L"Date modified", L"Type", L"Size", L"Dimensions", L"Duration"};
		constexpr std::array fields{
			files::SortField::name, files::SortField::modified, files::SortField::type,
			files::SortField::size, files::SortField::dimensions, files::SortField::duration
		};
		for (size_t index = 0; index < columns.size(); ++index)
		{
			const std::wstring label = fields[index] == sortField_
				                           ? std::wstring(labels[index]) + L" ^"
				                           : labels[index];
			recti text = columns[index];
			text.x += index == 0 ? metric(8) : metric(6);
			text.width = (std::max)(0, text.width - metric(12));
			renderer.text(label, text, platform::system_color(platform::SystemColor::buttonText),
			              platform::TextFormat::left | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
			if (index)
				renderer.outline({columns[index].x, columns[index].y, 1, columns[index].height},
				                 platform::system_color(platform::SystemColor::shadow));
		}
		renderer.outline(detailsHeaderBounds_, platform::system_color(platform::SystemColor::shadow));
	}

	void ImageCanvas::paint_details(const ui::CanvasRenderer& renderer)
	{
		for (int index = 0; index < static_cast<int>(files_.size()); ++index)
		{
			const recti row = item_rect(index);
			if (!row.intersects(itemsViewport_)) continue;
			const bool selected = active_selection().contains(index);
			renderer.fill(row, platform::system_color(selected
				                                          ? platform::SystemColor::highlight
				                                          : index == hoverIndex_
				                                          ? platform::SystemColor::face
				                                          : platform::SystemColor::window));
			const auto columns = media_detail_columns(row);
			const auto& item = items_[index];
			const auto& icon = detailTypeIcons_[static_cast<size_t>(item.kind)];
			if (!icon.pixels.empty())
				renderer.image({icon.width, icon.height, icon.pixels,
				                typeIconAlpha_[static_cast<size_t>(item.kind)]},
				               {columns[0].x + metric(4), row.y + metric(4), metric(32), metric(32)});
			const color textColor = platform::system_color(
				selected ? platform::SystemColor::highlightText : platform::SystemColor::windowText);
			recti name = columns[0];
			name.x += metric(42);
			name.width = (std::max)(0, name.width - metric(48));
			renderer.text(item.path.filename().wstring(), name, textColor,
			              platform::TextFormat::left | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
			const std::wstring modified = item.hasModified ? format::modified(item.modified) : L"";
			const std::wstring size = item.hasSize ? format::size(item.size) : L"";
			const std::wstring dimensions = format::dimensions(item.width, item.height);
			const std::array values{modified, format::item_type(item), size, dimensions,
				item.hasDuration ? media::duration_text(item.duration) : std::wstring{}};
			for (size_t column = 1; column < columns.size(); ++column)
			{
				recti text = columns[column];
				text.x += metric(6);
				text.width = (std::max)(0, text.width - metric(12));
				renderer.text(values[column - 1], text, textColor,
				              platform::TextFormat::left | platform::TextFormat::verticalCenter |
				              platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
			}
			if (index == active_selection().focus()) renderer.focus(row);
		}
	}

	void ImageCanvas::paint_tiles(const ui::CanvasRenderer& renderer)
	{
		for (int index = 0; index < static_cast<int>(files_.size()); ++index)
		{
			const recti tile = item_rect(index);
			if (!tile.intersects(itemsViewport_)) continue;
			const bool selected = active_selection().contains(index);
			renderer.fill(tile, platform::system_color(selected
				                                           ? platform::SystemColor::highlight
				                                           : index == hoverIndex_
				                                           ? platform::SystemColor::face
				                                           : platform::SystemColor::window));
			const recti imageArea{
				tile.x + metric(8), tile.y + metric(8), tile.width - metric(16), tile.height - metric(39)
			};
			const auto& thumb = thumbnails_[index];
			const auto* icon = thumb.pixels.empty()
				                   ? &typeIcons_[static_cast<size_t>(items_[index].kind)]
				                   : nullptr;
			const int visualWidth = icon ? icon->width : thumb.width;
			const int visualHeight = icon ? icon->height : thumb.height;
			const std::span<const std::uint32_t> visualPixels = icon ? icon->pixels : thumb.pixels;
			if (!visualPixels.empty())
			{
				const double scale = (std::min)(static_cast<double>(imageArea.width) / visualWidth,
				                                static_cast<double>(imageArea.height) / visualHeight);
				const int width = (std::max)(1, static_cast<int>(std::lround(visualWidth * scale)));
				const int height = (std::max)(1, static_cast<int>(std::lround(visualHeight * scale)));
				renderer.image({visualWidth, visualHeight, visualPixels,
				                icon && typeIconAlpha_[static_cast<size_t>(items_[index].kind)]},
				               {
					               imageArea.x + (imageArea.width - width) / 2,
					               imageArea.y + (imageArea.height - height) / 2, width, height
				               });
			}
			else
			{
				renderer.fill(imageArea, platform::system_color(platform::SystemColor::face));
			}
			const recti text{
				tile.x + metric(5), tile.bottom() - metric(27), tile.width - metric(10), metric(23)
			};
			const auto name = files_[index].filename().wstring();
			renderer.text(name, text, platform::system_color(
				              selected ? platform::SystemColor::highlightText : platform::SystemColor::windowText),
			              platform::TextFormat::center | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
			if (index == active_selection().focus()) renderer.focus(tile);
		}
	}
}
