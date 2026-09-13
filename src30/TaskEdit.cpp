// ImageWalker by Zac Walker
// Implements Photo Edit: a live draft over a scaled preview, and an explicit save.

#include "TaskEdit.h"

#include "ImageResampler.h"
#include "Paths.h"
#include "Undo.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <format>
#include <new>

namespace iw
{
	namespace
	{
		enum ControlId
		{
			idDescription = 1,
			idGeometryHeading,
			idStraighten,
			idRotateLeft,
			idRotateRight,
			idAutoStraighten,
			idResetGeometry,
			idColorHeading,
			idBrightness,
			idContrast,
			idDarks,
			idMidtones,
			idLights,
			idSaturation,
			idVibrance,
			idTemperature,
			idTint,
			idAutoColor,
			idResetColor,
			idSaveHeading,
			idBackup,
			idQuality,
			idWebpQuality,
			idWebpLossless,
			idPerspective,
			idAutoDocument,
			idCrop,
			idResetCrop
		};

		constexpr int previewLimit = 2400;

		std::optional<files::ImageSaveFormat> format_for(const std::filesystem::path& path)
		{
			const auto extension = paths::lowercase_extension(path);
			if (paths::iequals(extension, L".jpg") || paths::iequals(extension, L".jpeg"))
				return files::ImageSaveFormat::jpeg;
			if (paths::iequals(extension, L".bmp")) return files::ImageSaveFormat::bmp;
			if (paths::iequals(extension, L".tif") || paths::iequals(extension, L".tiff"))
				return files::ImageSaveFormat::tiff;
			if (paths::iequals(extension, L".png")) return files::ImageSaveFormat::png;
			if (paths::iequals(extension, L".webp")) return files::ImageSaveFormat::webp;
			return {};
		}

		bool format_keeps_edits(const std::filesystem::path& path)
		{
			const auto format = format_for(path);
			const auto formats = files::writable_image_formats();
			return format && std::ranges::find(formats, *format) != formats.end();
		}

		bool valid_image(const files::DecodedImage& image)
		{
			return image.width > 0 && image.height > 0 &&
				static_cast<std::uint64_t>(image.width) * image.height <= image.pixels.size();
		}

		void draw_edge(const ui::CanvasRenderer& renderer, pointi from, const pointi to,
			const color value, const int thickness)
		{
			// The renderer facade exposes filled rectangles, not native line primitives.
			// Coalesce Bresenham pixels into horizontal runs to avoid one draw per pixel.
			const int dx = std::abs(to.x - from.x), sx = from.x < to.x ? 1 : -1;
			const int dy = -std::abs(to.y - from.y), sy = from.y < to.y ? 1 : -1;
			int error = dx + dy;
			pointi run = from;
			for (;;)
			{
				const auto previous = from;
				if (from == to)
				{
					renderer.fill({(std::min)(run.x, from.x), from.y,
						std::abs(from.x - run.x) + thickness, thickness}, value);
					break;
				}
				const int twice = error * 2;
				if (twice >= dy) { error += dy; from.x += sx; }
				if (twice <= dx) { error += dx; from.y += sy; }
				if (from.y != previous.y)
				{
					renderer.fill({(std::min)(run.x, previous.x), previous.y,
						std::abs(previous.x - run.x) + thickness, thickness}, value);
					run = from;
				}
			}
		}

	}

	std::filesystem::path proposed_edit_path(const std::filesystem::path& source)
	{
		if (source.empty() || source.filename().empty()) return {};
		const auto extension = source.extension().wstring();
		const auto first = source.parent_path() / (source.stem().wstring() + L"-edit" + extension);
		std::error_code error;
		const bool exists = std::filesystem::exists(files::native_path(first), error);
		if (error) return {};
		if (!exists) return first;
		for (int number = 2; number < 4096; ++number)
		{
			const auto candidate = source.parent_path() /
				std::format(L"{}-edit {}{}", source.stem().wstring(), number, extension);
			error.clear();
			const bool taken = std::filesystem::exists(files::native_path(candidate), error);
			if (error) return {};
			if (!taken) return candidate;
		}
		return {};
	}

	void TaskEdit::set_photo(const std::filesystem::path& path)
	{
		const auto found = std::ranges::find_if(targets_, [&path](const std::filesystem::path& value)
		{
			return paths::equal(value, path);
		});
		index_ = found == targets_.end() ? 0 : static_cast<size_t>(found - targets_.begin());
		path_ = path;
	}

	void TaskEdit::build_controls()
	{
		const auto slider = [this](const int id, const wchar_t* label, const int minimum,
		                                    const int maximum, const int value)
		{
			ui::Control control;
			control.id = id;
			control.kind = ui::ControlKind::slider;
			control.label = label;
			control.minimum = minimum;
			control.maximum = maximum;
			control.value = value;
			control.changed = [this, id] { controlGroup_ = id; controls_changed(); };
			panel_.add(std::move(control));
		};
		const auto button = [this](const int id, const wchar_t* label)
		{
			ui::Control control;
			control.id = id;
			control.kind = ui::ControlKind::button;
			control.label = label;
			panel_.add(std::move(control));
		};
		const auto heading = [this](const int id, const wchar_t* label)
		{
			ui::Control control;
			control.id = id;
			control.kind = ui::ControlKind::heading;
			control.label = label;
			panel_.add(std::move(control));
		};

		ui::Control description;
		description.id = idDescription;
		description.kind = ui::ControlKind::label;
		description.label =
			L"Drag crop handles, adjust perspective corners, or detect a document. Preview shows "
			L"the saved result. Nothing is written until you save.";
		panel_.add(std::move(description));

		heading(idGeometryHeading, L"Geometry");
		slider(idStraighten, L"Straighten", -100, 100, 0);
		button(idRotateLeft, L"Rotate anticlockwise");
		button(idRotateRight, L"Rotate clockwise");
		button(idAutoStraighten, L"Auto straighten");
		button(idPerspective, L"Adjust perspective corners");
		button(idAutoDocument, L"Detect document");
		button(idCrop, L"Adjust crop handles");
		button(idResetCrop, L"Reset crop");
		button(idResetGeometry, L"Reset all geometry");

		heading(idColorHeading, L"Colour");
		slider(idBrightness, L"Brightness", -100, 100, 0);
		slider(idContrast, L"Contrast", -100, 100, 0);
		slider(idDarks, L"Darks", -100, 100, 0);
		slider(idMidtones, L"Midtones", -100, 100, 0);
		slider(idLights, L"Lights", -100, 100, 0);
		slider(idSaturation, L"Saturation", -100, 100, 0);
		slider(idVibrance, L"Vibrance", -100, 100, 0);
		slider(idTemperature, L"Temperature", -100, 100, 0);
		slider(idTint, L"Tint", -100, 100, 0);
		button(idAutoColor, L"Auto colour");
		button(idResetColor, L"Reset colour adjustments");

		heading(idSaveHeading, L"Saving");
		ui::Control backup;
		backup.id = idBackup;
		backup.kind = ui::ControlKind::checkBox;
		backup.label = L"When overwriting a photo with irreversible changes make an original backup copy";
		backupOnOverwrite_ = platform::read_integer_setting(L"Edit", L"BackupOnOverwrite", 1) != 0;
		backup.checked = backupOnOverwrite_;
		backup.changed = [this] { persist_options(); };
		panel_.add(std::move(backup));
		jpegQuality_ = std::clamp(platform::read_integer_setting(L"Edit", L"JpegQuality", 90), 10, 100);
		slider(idQuality, L"JPEG save quality", 10, 100, jpegQuality_);
		if (auto* quality = panel_.find(idQuality))
		{
			quality->suffix = L"%";
			quality->changed = [this] { persist_options(); };
		}
		if (format_keeps_edits(L"photo.webp"))
		{
			webpQuality_ = std::clamp(platform::read_integer_setting(L"Edit", L"WebpQuality", 90), 1, 100);
			webpLossless_ = platform::read_integer_setting(L"Edit", L"WebpLossless", 0) != 0;
			slider(idWebpQuality, L"WebP save quality", 1, 100, webpQuality_);
			if (auto* quality = panel_.find(idWebpQuality))
			{
				quality->suffix = L"%";
				quality->changed = [this] { persist_options(); };
			}
			ui::Control lossless;
			lossless.id = idWebpLossless;
			lossless.kind = ui::ControlKind::checkBox;
			lossless.label = L"WebP lossless compression";
			lossless.checked = webpLossless_;
			lossless.changed = [this] { persist_options(); };
			panel_.add(std::move(lossless));
			panel_.set_enabled(idWebpQuality, !webpLossless_);
		}
		if (auto* straighten = panel_.find(idStraighten)) straighten->suffix = L" tenths";

		write_controls();
		if (source_.width <= 0) load_photo();
	}

	void TaskEdit::write_controls()
	{
		panel_.set_value(idStraighten, edits_.straighten);
		panel_.set_value(idBrightness, edits_.brightness);
		panel_.set_value(idContrast, edits_.contrast);
		panel_.set_value(idDarks, edits_.darks);
		panel_.set_value(idMidtones, edits_.midtones);
		panel_.set_value(idLights, edits_.lights);
		panel_.set_value(idSaturation, edits_.saturation);
		panel_.set_value(idVibrance, edits_.vibrance);
		panel_.set_value(idTemperature, edits_.temperature);
		panel_.set_value(idTint, edits_.tint);
	}

	void TaskEdit::read_controls()
	{
		const auto value = [this](const int id, int& into)
		{
			if (const auto* control = panel_.find(id)) into = control->value;
		};
		value(idStraighten, edits_.straighten);
		value(idBrightness, edits_.brightness);
		value(idContrast, edits_.contrast);
		value(idDarks, edits_.darks);
		value(idMidtones, edits_.midtones);
		value(idLights, edits_.lights);
		value(idSaturation, edits_.saturation);
		value(idVibrance, edits_.vibrance);
		value(idTemperature, edits_.temperature);
		value(idTint, edits_.tint);
	}

	tasks::TaskRunner::AnalyzeFunction TaskEdit::make_analyzer()
	{
		return [](const std::stop_token&, const tasks::ProgressFunction&) { return tasks::TaskPlan{}; };
	}

	void TaskEdit::load_photo()
	{
		finish_drag(true);
		history_.clear();
		perspectiveHandles_ = false;
		imageBounds_ = {};
		const auto generation = ++loadGeneration_;
		source_ = {};
		sourceSnapshot_.reset();
		previewSource_.reset();
		if (path_.empty() || !files::is_editable_image(path_))
		{
			loading_ = false;
			panel_.set_interactive(false);
			set_status(path_.empty() ? L"No editable photo." :
				L"This file is no longer an editable photo. It may be read-only, missing or unsupported.");
			refresh_commands();
			invalidate();
			return;
		}
		loading_ = true;
		panel_.set_interactive(false);
		set_status(std::format(L"Loading {}...", path_.filename().wstring()));
		refresh_commands();
		const auto weak = weak_from_this();
		const auto path = path_;
		if (!platform::queue_work(platform::WorkQueue::image, [weak, path, generation]
		{
			files::DecodedImage decoded;
			std::optional<files::FileSnapshot> snapshot;
			try
			{
				snapshot = files::snapshot_file(path);
				if (snapshot && snapshot->exists && !snapshot->directory && !snapshot->readOnly)
					decoded = files::load_image_for_area(path, previewLimit, previewLimit);
				if (!snapshot || !files::matches_snapshot(path, *snapshot)) decoded = {};
			}
			catch (...) { decoded = {}; }
			platform::queue_ui([weak, path, generation, snapshot, decoded = std::move(decoded)]() mutable
			{
				const auto self = std::static_pointer_cast<TaskEdit>(weak.lock());
				if (!self || self->loadGeneration_ != generation || !paths::equal(self->path_, path)) return;
				self->loading_ = false;
				self->panel_.set_interactive(true);
				self->source_ = std::move(decoded);
				self->sourceSnapshot_ = snapshot;
				self->previewSource_.reset();
				self->set_status_for_photo();
				self->refresh_commands();
				self->invalidate();
			});
		}))
		{
			loading_ = false;
			panel_.set_interactive(true);
			set_status(L"The image worker is unavailable. Close Edit and try again.");
			refresh_commands();
		}
	}

	void TaskEdit::set_status_for_photo()
	{
		if (path_.empty())
		{
			set_status(L"No editable photo.");
			return;
		}
		if (loading_)
		{
			set_status(std::format(L"Loading {}...", path_.filename().wstring()));
			return;
		}
		const sizei original{source_.originalWidth > 0 ? source_.originalWidth : source_.width,
			source_.originalHeight > 0 ? source_.originalHeight : source_.height};
		const auto frame = edits::transformed_size(original, edits_);
		const auto crop = edits::effective_crop(original, edits_);
		set_status(source_.width > 0
			           ? std::format(L"{} - {} of {} - {} x {}{}", path_.filename().wstring(), index_ + 1,
			                         targets_.size(), crop.width ? crop.width : frame.width,
			                         crop.height ? crop.height : frame.height,
			                         dirty() ? L" - modified" : L"")
			           : std::format(L"{} could not be decoded.", path_.filename().wstring()));
	}

	void TaskEdit::rebuild_preview(const sizei pane)
	{
		const bool swaps = (edits_.rotation % 2) != 0;
		previewSource_.update(source_, swaps ? sizei{pane.height, pane.width} : pane);
	}

	void TaskEdit::controls_changed()
	{
		if (loading_ || saving_) return;
		const auto previous = edits_;
		read_controls();
		history_.record(previous, edits_, controlGroup_);
		set_status_for_photo();
		refresh_commands();
		invalidate();
	}

	void TaskEdit::control_activated(ui::Control& control)
	{
		if (loading_ || saving_ || !valid_image(source_)) return;
		finish_drag();
		const auto previous = edits_;
		switch (control.id)
		{
		case idRotateLeft:
			edits_.rotation = ((edits_.rotation - 1) % 4 + 4) % 4;
			edits_.crop = {};
			perspectiveHandles_ = false;
			break;
		case idRotateRight:
			edits_.rotation = (edits_.rotation + 1) % 4;
			edits_.crop = {};
			perspectiveHandles_ = false;
			break;
		case idPerspective:
			perspectiveHandles_ = true;
			showResult_ = false;
			set_status(L"Drag the four corners on the original photo, clockwise from top-left. Preview shows the corrected result.");
			invalidate();
			return;
		case idCrop:
			perspectiveHandles_ = false;
			showResult_ = false;
			set_status(L"Drag an edge or corner to crop; drag inside to move the crop. Preview shows the saved result.");
			invalidate();
			return;
		case idResetCrop:
			edits_.crop = {};
			break;
		case idAutoDocument:
			if (const auto corners = edits::detect_document(source_))
			{
				if (edits::perspective_size(original_size(), *corners).width < 2)
				{
					set_status(L"The detected perspective exceeds the supported image size. Adjust the corners manually.");
					return;
				}
				edits_.perspective = corners;
				edits_.crop = {};
				edits_.straighten = 0;
				perspectiveHandles_ = true;
				showResult_ = false;
				write_controls();
			}
			else
			{
				set_status(L"No document detected. A complete, contrasting four-sided page is required; adjust perspective corners manually.");
				return;
			}
			break;
		case idAutoStraighten:
		{
			// The preview is what the user is judging, and the tilt it carries is the file's.
			auto geometry = edits_;
			geometry.straighten = 0;
			geometry.crop = {};
			geometry.reset_color();
			try
			{
				const auto current = edits::apply(source_, geometry, false);
				if (!valid_image(current))
				{
					set_status(L"The perspective could not be rendered within the image memory limits. The draft has been kept.");
					return;
				}
				auto candidate = edits_;
				if (!edits::auto_straighten(current, candidate))
				{
					set_status(L"This photo has too few straight edges to straighten automatically.");
					return;
				}
				edits_ = candidate;
			}
			catch (const std::bad_alloc&)
			{
				set_status(L"There is not enough memory to straighten this photo. The draft has been kept.");
				return;
			}
			write_controls();
			break;
		}
		case idAutoColor:
			if (!edits::auto_color(source_, edits_))
			{
				set_status(L"This photo has no visible pixels to adjust automatically.");
				return;
			}
			write_controls();
			break;
		case idResetGeometry:
			edits_.reset_geometry();
			perspectiveHandles_ = false;
			write_controls();
			break;
		case idResetColor:
			edits_.reset_color();
			write_controls();
			break;
		default:
			TaskView::control_activated(control);
			return;
		}
		history_.record(previous, edits_);
		set_status_for_photo();
		refresh_commands();
		invalidate();
	}

	std::vector<platform::ToolbarItem> TaskEdit::toolbar_items()
	{
		const auto make = [](std::wstring name, std::function<void()> invoke,
		                     std::function<bool()> enabled = {})
		{
			auto command = std::make_shared<platform::Command>();
			command->name = std::move(name);
			command->tooltip = command->name;
			command->toolbarText = [text = command->name] { return text; };
			command->invoke = std::move(invoke);
			command->enabled = std::move(enabled);
			return command;
		};

		auto preview = make(L"Preview", [this]
		{
			finish_drag();
			showResult_ = !showResult_;
			invalidate();
		});
		preview->checked = [this] { return showResult_; };
		auto previous = make(L"Save and open previous", [this] { step_photo(-1, true); },
		                     [this] { return !saving_ && !loading_ && targets_.size() > 1; });
		auto next = make(L"Save and open next", [this] { step_photo(1, true); },
		                 [this] { return !saving_ && !loading_ && targets_.size() > 1; });
		auto saveAs = make(L"Save as", [this] { save_as(); },
			[this] { return !saving_ && !loading_ && valid_image(source_); });
		auto saveCommand = make(L"Save", [this] { save(); },
			[this] { return !saving_ && !loading_ && dirty() && valid_image(source_); });
		auto undoCommand = make(L"Undo", [this] { undo(false); },
			[this] { return !saving_ && !loading_ && history_.can_undo(); });
		auto redoCommand = make(L"Redo", [this] { undo(true); },
			[this] { return !saving_ && !loading_ && history_.can_redo(); });
		std::vector<platform::ToolbarItem> items{
			platform::ToolbarItem::action(preview), platform::ToolbarItem::separator(),
			platform::ToolbarItem::action(undoCommand), platform::ToolbarItem::action(redoCommand),
			platform::ToolbarItem::separator(),
			platform::ToolbarItem::action(previous), platform::ToolbarItem::action(next),
			platform::ToolbarItem::action(saveAs), platform::ToolbarItem::action(saveCommand),
			platform::ToolbarItem::separator()
		};
		append_window_commands(items);
		return items;
	}

	void TaskEdit::draw_content(const ui::CanvasRenderer& renderer, const recti bounds)
	{
		imageBounds_ = {};
		const color face = platform::system_color(platform::SystemColor::face);
		const color shadow = platform::system_color(platform::SystemColor::shadow);
		const color gray = platform::system_color(platform::SystemColor::grayText);

		const int stripHeight = targets_.size() > 1 ? (std::min)(metric(72), bounds.height) : 0;
		photoBounds_ = {bounds.x, bounds.y, bounds.width, (std::max)(0, bounds.height - stripHeight)};
		stripBounds_ = {
			bounds.x, photoBounds_.bottom(), bounds.width, (std::max)(0, stripHeight - metric(4))
		};
		renderer.fill(photoBounds_, face);
		renderer.outline(photoBounds_, shadow);

		if (stripHeight > 0)
		{
			const int cell = (std::max)(metric(24), stripBounds_.height);
			const auto visible = static_cast<size_t>((std::max)(1, stripBounds_.width / (cell + metric(4))));
			const size_t first = index_ >= visible ? index_ - visible + 1 : 0;
			int x = stripBounds_.x;
			for (size_t item = first; item < targets_.size(); ++item)
			{
				if (x + cell > stripBounds_.right()) break;
				const recti tile{x, stripBounds_.y, cell, stripBounds_.height};
				renderer.fill(tile, item == index_
					? platform::system_color(platform::SystemColor::highlight) : face);
				renderer.outline(tile, shadow);
				renderer.text(targets_[item].stem().wstring(), tile,
					item == index_ ? platform::system_color(platform::SystemColor::highlightText)
					               : platform::system_color(platform::SystemColor::windowText),
					platform::TextFormat::center | platform::TextFormat::verticalCenter |
						platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
				x += cell + metric(4);
			}
		}

		if (source_.width <= 0)
		{
			renderer.text(loading_ ? L"Loading..." : L"No photo to edit.", photoBounds_, gray,
			              platform::TextFormat::center | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine);
			return;
		}

		rebuild_preview({photoBounds_.width - metric(16), photoBounds_.height - metric(16)});
		const auto& preview = previewSource_.image();
		if (preview.width <= 0) return;
		// Preview off keeps the whole straightened frame so the emptied corners stay visible.
		const auto previewEdits = edits::scaled_edits(edits_,
			{source_.originalWidth > 0 ? source_.originalWidth : source_.width,
			 source_.originalHeight > 0 ? source_.originalHeight : source_.height},
			{preview.width, preview.height});
		const bool selectingCorners = perspectiveHandles_ && !showResult_;
		files::DecodedImage shown;
		try { shown = selectingCorners ? preview : edits::apply(preview, previewEdits, showResult_); }
		catch (const std::bad_alloc&)
		{
			renderer.text(L"Not enough memory to render this draft.", photoBounds_, gray,
				platform::TextFormat::center | platform::TextFormat::singleLine);
			return;
		}
		if (!valid_image(shown))
		{
			renderer.text(L"The perspective exceeds the supported image size or cannot be rendered.", photoBounds_, gray,
				platform::TextFormat::center | platform::TextFormat::singleLine);
			return;
		}
		const recti fitted = ui::fit_centered({shown.width, shown.height}, {
			                                      photoBounds_.x + metric(8), photoBounds_.y + metric(8),
			                                      (std::max)(1, photoBounds_.width - metric(16)),
			                                      (std::max)(1, photoBounds_.height - metric(16))
		                                      });
		renderer.image({shown.width, shown.height, shown.pixels, true, 0}, fitted);
		renderer.outline(fitted, shadow);
		imageBounds_ = fitted;
		if (showResult_ || loading_ || saving_) return;
		const auto highlight = platform::system_color(platform::SystemColor::highlight);
		const auto handle = [&](const pointi p)
		{
			const int radius = metric(5);
			const recti box{p.x - radius, p.y - radius, radius * 2 + 1, radius * 2 + 1};
			renderer.fill(box, platform::system_color(platform::SystemColor::window));
			renderer.outline(box, highlight, metric(2));
		};
		if (selectingCorners)
		{
			const auto corners = edits_.perspective.value_or(edits::whole_picture);
			std::array<pointi, 4> points;
			for (size_t i = 0; i < corners.size(); ++i)
				points[i] = {fitted.x + static_cast<int>(std::lround(corners[i].x * (fitted.width - 1))),
					fitted.y + static_cast<int>(std::lround(corners[i].y * (fitted.height - 1)))};
			for (size_t i = 0; i < points.size(); ++i)
				draw_edge(renderer, points[i], points[(i + 1) % 4], highlight, metric(2));
			for (const auto p : points) handle(p);
			renderer.text(L"Original photo - perspective corners", {fitted.x, photoBounds_.y, fitted.width, metric(22)},
				gray, platform::TextFormat::center | platform::TextFormat::singleLine);
		}
		else
		{
			const auto crop = displayed_crop();
			renderer.outline(crop, highlight, metric(2));
			for (const int x : {crop.x, crop.x + crop.width / 2, crop.right()})
				for (const int y : {crop.y, crop.y + crop.height / 2, crop.bottom()})
					if (x != crop.x + crop.width / 2 || y != crop.y + crop.height / 2) handle({x, y});
		}
	}

	sizei TaskEdit::original_size() const
	{
		return {source_.originalWidth > 0 ? source_.originalWidth : source_.width,
			source_.originalHeight > 0 ? source_.originalHeight : source_.height};
	}

	recti TaskEdit::displayed_crop() const
	{
		const auto frame = edits::transformed_size(original_size(), edits_);
		const auto crop = edits::effective_crop(original_size(), edits_);
		if (frame.width <= 0 || frame.height <= 0 || imageBounds_.width <= 0 || imageBounds_.height <= 0)
			return {};
		const auto x = [&](const int p)
			{ return imageBounds_.x + static_cast<int>(static_cast<double>(p) * imageBounds_.width / frame.width); };
		const auto y = [&](const int p)
			{ return imageBounds_.y + static_cast<int>(static_cast<double>(p) * imageBounds_.height / frame.height); };
		return {x(crop.x), y(crop.y), x(crop.right()) - x(crop.x), y(crop.bottom()) - y(crop.y)};
	}

	void TaskEdit::finish_drag(const bool cancel)
	{
		if (!content_dragging()) return;
		if (cancel) edits_ = beforeDrag_;
		else history_.record(beforeDrag_, edits_);
		cropDrag_ = edits::CropHandle::none;
		cornerDrag_ = -1;
		if (frame_) frame_->release_capture();
		set_status_for_photo();
		refresh_commands();
		invalidate();
	}

	void TaskEdit::undo(const bool redo)
	{
		if (loading_ || saving_) return;
		finish_drag();
		if (!(redo ? history_.redo(edits_) : history_.undo(edits_))) return;
		write_controls();
		set_status_for_photo();
		refresh_commands();
		invalidate();
	}

	bool TaskEdit::content_mouse(const platform::MouseMessage message, const platform::MouseInput& input)
	{
		if (message == platform::MouseMessage::leave)
		{
			finish_drag(true);
			return true;
		}
		if (content_dragging())
		{
			if (message == platform::MouseMessage::leftButtonUp)
			{
				auto released = input;
				released.leftButton = true;
				content_mouse(platform::MouseMessage::move, released);
				finish_drag();
				return true;
			}
			if (message == platform::MouseMessage::leftButtonDown) { finish_drag(); }
			else if (message == platform::MouseMessage::move)
			{
				if (!input.leftButton) { finish_drag(); return true; }
				if (input.point == dragStart_) edits_ = beforeDrag_;
				else if (cornerDrag_ >= 0 && imageBounds_.width > 1 && imageBounds_.height > 1)
				{
					auto corners = edits_.perspective.value_or(edits::whole_picture);
					corners[static_cast<size_t>(cornerDrag_)] = {
						std::clamp(static_cast<double>(input.point.x - imageBounds_.x) / (imageBounds_.width - 1), 0.0, 1.0),
						std::clamp(static_cast<double>(input.point.y - imageBounds_.y) / (imageBounds_.height - 1), 0.0, 1.0)};
					if (edits::valid_quadrilateral(corners) &&
						edits::perspective_size(original_size(), corners).width >= 2)
					{
						edits_.perspective = corners;
						edits_.crop = {};
					}
				}
				else if (imageBounds_.width > 0 && imageBounds_.height > 0)
				{
					const auto size = edits::transformed_size(original_size(), edits_);
					const pointi delta{
						static_cast<int>(std::lround(static_cast<double>(input.point.x - dragStart_.x) * size.width / imageBounds_.width)),
						static_cast<int>(std::lround(static_cast<double>(input.point.y - dragStart_.y) * size.height / imageBounds_.height))};
					const auto moved = edits::drag_crop(dragCrop_, edits::crop_bounds(original_size(), edits_), cropDrag_, delta);
					edits_.crop = moved == dragCrop_ ? beforeDrag_.crop : moved;
				}
				set_status_for_photo();
				invalidate();
				return true;
			}
		}
		if (message == platform::MouseMessage::leftButtonDown && !loading_ && !saving_ &&
			!showResult_ && imageBounds_.width > 1 && imageBounds_.height > 1 && valid_image(source_))
		{
			if (perspectiveHandles_)
			{
				const auto corners = edits_.perspective.value_or(edits::whole_picture);
				for (size_t i = 0; i < corners.size(); ++i)
				{
					const pointi p{imageBounds_.x + static_cast<int>(std::lround(corners[i].x * (imageBounds_.width - 1))),
						imageBounds_.y + static_cast<int>(std::lround(corners[i].y * (imageBounds_.height - 1)))};
					if (std::abs(input.point.x - p.x) <= metric(9) && std::abs(input.point.y - p.y) <= metric(9))
					{ cornerDrag_ = static_cast<int>(i); break; }
				}
			}
			else cropDrag_ = edits::crop_handle(displayed_crop(), input.point, metric(8));
			if (content_dragging())
			{
				beforeDrag_ = edits_;
				dragStart_ = input.point;
				dragCrop_ = edits::effective_crop(original_size(), edits_);
				if (frame_) frame_->set_capture();
				return true;
			}
		}
		if (message != platform::MouseMessage::leftButtonDown || !stripBounds_.contains(input.point))
			return false;
		if (loading_ || saving_) return true;
		const int cell = (std::max)(metric(24), stripBounds_.height) + metric(4);
		const auto visible = static_cast<size_t>((std::max)(1, stripBounds_.width / cell));
		const size_t first = index_ >= visible ? index_ - visible + 1 : 0;
		const size_t item = first + static_cast<size_t>((std::max)(0, input.point.x - stripBounds_.x) / cell);
		if (item >= targets_.size() || item == index_) return true;
		return step_photo(static_cast<int>(item) - static_cast<int>(index_), false);
	}

	bool TaskEdit::content_key(const platform::KeyInput& input)
	{
		if (input.key == platform::KeyCode::escape && content_dragging()) { finish_drag(true); return true; }
		if (input.control && std::towupper(input.character) == L'Z') { undo(input.shift); return true; }
		if (input.control && std::towupper(input.character) == L'Y') { undo(true); return true; }
		if (input.key == platform::KeyCode::left) return step_photo(-1, false);
		if (input.key == platform::KeyCode::right) return step_photo(1, false);
		return false;
	}

	TaskEdit::Leaving TaskEdit::ask_about_draft()
	{
		if (!dirty()) return Leaving::discard;
		platform::ChoiceDefinition definition;
		definition.title = title();
		definition.heading = std::format(L"{} has modifications.", path_.filename().wstring());
		definition.message = L"Save them before leaving this photo?";
		definition.buttons = {{1, L"Save"}, {2, L"Don't Save"}, {3, L"Cancel"}};
		definition.defaultButton = 1;
		const int chosen = platform::show_choice(frame_, definition);
		if (chosen == 1) return Leaving::save;
		return chosen == 2 ? Leaving::discard : Leaving::cancel;
	}

	void TaskEdit::persist_options()
	{
		if (const auto* backup = panel_.find(idBackup)) backupOnOverwrite_ = backup->checked;
		if (const auto* quality = panel_.find(idQuality)) jpegQuality_ = std::clamp(quality->value, 10, 100);
		std::vector<platform::IntegerSetting> settings{
			platform::IntegerSetting{L"JpegQuality", jpegQuality_},
			platform::IntegerSetting{L"BackupOnOverwrite", backupOnOverwrite_ ? 1 : 0}
		};
		if (const auto* quality = panel_.find(idWebpQuality))
		{
			webpQuality_ = std::clamp(quality->value, 1, 100);
			settings.push_back({L"WebpQuality", webpQuality_});
		}
		if (const auto* lossless = panel_.find(idWebpLossless))
		{
			webpLossless_ = lossless->checked;
			settings.push_back({L"WebpLossless", webpLossless_ ? 1 : 0});
			panel_.set_enabled(idWebpQuality, !webpLossless_);
		}
		platform::write_integer_settings(L"Edit", settings);
	}

	bool TaskEdit::save_to(const std::filesystem::path& destination, const bool inPlace)
	{
		finish_drag();
		const auto keepAlive = weak_from_this().lock();
		saveError_.clear();
		if (loading_ || saving_ || !valid_image(source_))
		{
			saveError_ = L"The full photo is not available yet. The draft has been kept.";
			return false;
		}
		const auto fail = [this](std::wstring message)
		{
			saveError_ = std::move(message);
			return false;
		};
		if (inPlace && !paths::equal(path_, destination))
			return fail(L"An in-place save must keep the original filename.");
		const auto format = format_for(destination);
		if (!format || !format_keeps_edits(destination))
			return fail(L"Choose an image filename supported by an available encoder.");
		const auto destinationSnapshot = files::snapshot_file(destination);
		if (!destinationSnapshot) return fail(L"The destination could not be checked. Nothing was saved.");
		const bool exists = destinationSnapshot->exists;
		std::error_code error;
		if (!inPlace)
		{
			if (paths::equal(path_, destination) ||
				(exists && std::filesystem::equivalent(files::native_path(path_),
					files::native_path(destination), error)))
				return fail(L"Save as must use a different file so the original is left unchanged.");
			if (error) return fail(L"The destination could not be checked. Nothing was saved.");
		}
		if (exists)
		{
			if (destinationSnapshot->directory || destinationSnapshot->readOnly)
				return fail(L"The destination is read-only or is not a regular file. Choose another filename.");
		}
		if (!sourceSnapshot_ || !files::matches_snapshot(path_, *sourceSnapshot_))
			return fail(L"The source photo changed or is no longer available. Nothing was saved; the draft has been kept.");
		if (!inPlace && exists)
		{
			platform::ChoiceDefinition definition;
			definition.title = title();
			definition.heading = std::format(L"Replace {}?", destination.filename().wstring());
			definition.message = L"This existing file will be replaced by the edited photo.";
			definition.buttons = {{1, L"Replace"}, {2, L"Cancel"}};
			definition.defaultButton = 2;
			definition.warning = true;
			if (platform::show_choice(frame_, definition) != 1) return false;
		}
		saving_ = true;
		struct SavingGuard
		{
			TaskEdit& task;
			~SavingGuard()
			{
				task.saving_ = false;
				task.panel_.set_interactive(true);
				task.refresh_commands();
			}
		} guard{*this};
		panel_.set_interactive(false);
		refresh_commands();
		set_status(std::format(L"Saving {}...", destination.filename().wstring()));
		try
		{
			const bool backup = inPlace && edits_.is_irreversible() && backupOnOverwrite_;
			files::SaveOptions options;
			options.quality = *format == files::ImageSaveFormat::webp ? webpQuality_ : jpegQuality_;
			options.lossless = *format == files::ImageSaveFormat::webp && webpLossless_;
			const auto workerError = std::make_shared<std::wstring>();
			const bool saved = platform::run_with_status(frame_, title(),
				std::format(L"Saving {}...", destination.filename().wstring()),
				[source = path_, sourceSnapshot = *sourceSnapshot_, destination,
				 destinationSnapshot = *destinationSnapshot, value = edits_, backup, options,
				 format = *format, workerError]
				{
					const auto unchanged = [&]
					{
						if (!files::matches_snapshot(source, sourceSnapshot))
						{
							*workerError = L"The source photo changed while saving. Nothing was saved; the draft has been kept.";
							return false;
						}
						if (!files::matches_snapshot(destination, destinationSnapshot))
						{
							*workerError = L"The destination changed while saving. Nothing was replaced.";
							return false;
						}
						return true;
					};
					if (!unchanged()) return false;
					// Never substitute the scaled preview when the full-resolution decode fails.
					const auto full = files::load_image(source);
					if (!valid_image(full))
					{
						*workerError = L"The full-resolution photo could not be decoded. Nothing was saved.";
						return false;
					}
					const auto rendered = edits::apply(full, value);
					if (!valid_image(rendered))
					{
						*workerError = L"These edits leave no usable image. Adjust or reset the geometry.";
						return false;
					}
					if (!unchanged()) return false;
					auto journal = undo::history().begin(L"Photo Edit");
					if (!journal->protect({destination}, *workerError)) return false;
					const bool written = files::save_image(rendered, destination, format, options, destinationSnapshot.exists, [&]
					{
						if (!unchanged() || !journal->validate_before({destination}, *workerError)) return false;
						if (backup && files::create_original_backup(destination).empty())
						{
							*workerError = L"The original backup copy could not be written, so nothing was saved.";
							return false;
						}
						return unchanged() && journal->validate_before({destination}, *workerError);
					});
					std::wstring recordError;
					if (written && !journal->completed({destination}, recordError))
						*workerError = L"The photo was saved, but Undo recording failed. " + recordError;
					if (!journal->finish(recordError))
					{
						if (!workerError->empty()) *workerError += L" ";
						*workerError += written ? L"The photo was saved. " : L"Nothing was saved. ";
						*workerError += recordError;
					}
					return written;
				});
			if (!saved)
				return fail(workerError->empty() ? L"The photo could not be saved. The draft has been kept."
				                               : std::move(*workerError));
			if (!workerError->empty()) platform::show_error(*workerError, L"Photo Edit recovery", frame_);
			return saved;
		}
		catch (...)
		{
			return fail(L"The photo could not be saved. The draft has been kept.");
		}
	}

	void TaskEdit::report_save_error()
	{
		set_status_for_photo();
		if (!saveError_.empty()) platform::show_error(saveError_, title(), frame_);
	}

	void TaskEdit::saved_as(const std::filesystem::path& destination)
	{
		edits_ = {};
		history_.clear();
		write_controls();
		persist_options();
		if (host_.saved)
		{
			closingAfterSave_ = true;
			++loadGeneration_;
			platform::queue_ui([saved = host_.saved, destination] { saved(destination, true); });
		}
		else
		{
			targets_ = {destination};
			set_photo(destination);
			load_photo();
		}
	}

	bool TaskEdit::save_as_jpeg()
	{
		platform::ChoiceDefinition definition;
		definition.title = title();
		definition.heading = std::format(L"{} cannot store these changes.", path_.extension().wstring());
		definition.message = L"The photo must be saved as a JPEG file instead. Choose a new filename.";
		definition.buttons = {{1, L"As JPEG"}, {2, L"Cancel"}};
		definition.defaultButton = 1;
		if (platform::show_choice(frame_, definition) != 1) return false;
		auto jpeg = path_;
		jpeg.replace_extension(L".jpg");
		const auto proposed = proposed_edit_path(jpeg);
		if (proposed.empty())
		{
			platform::show_error(L"A free JPEG filename could not be found.", title(), frame_);
			return false;
		}
		const auto chosen = platform::choose_save_file(frame_,
			{L"JPEG image", L"*.jpg;*.jpeg", L".jpg", proposed.filename().wstring()});
		if (!chosen) return false;
		if (format_for(*chosen) != files::ImageSaveFormat::jpeg)
		{
			platform::show_error(L"Choose a .jpg or .jpeg filename for a JPEG image.", title(), frame_);
			return false;
		}
		if (!save_to(*chosen, false))
		{
			report_save_error();
			return false;
		}
		saved_as(*chosen);
		return true;
	}

	bool TaskEdit::save()
	{
		if (saving_) return false;
		finish_drag();
		if (!dirty()) return true;
		if (loading_ || !valid_image(source_)) return false;
		if (!format_keeps_edits(path_)) return save_as_jpeg();
		if (!save_to(path_, true))
		{
			report_save_error();
			return false;
		}
		edits_ = {};
		write_controls();
		persist_options();
		if (host_.saved)
			platform::queue_ui([saved = host_.saved, path = path_] { saved(path, false); });
		load_photo();
		return true;
	}

	bool TaskEdit::save_as()
	{
		if (saving_ || loading_ || !valid_image(source_)) return false;
		if (!format_keeps_edits(path_)) return save_as_jpeg();
		const auto proposed = proposed_edit_path(path_);
		if (proposed.empty())
		{
			platform::show_error(L"A free Save as filename could not be found.", title(), frame_);
			return false;
		}
		std::wstring filter = L"*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff";
		if (format_keeps_edits(L"photo.webp")) filter += L";*.webp";
		const auto chosen = platform::choose_save_file(frame_, {
			                                               L"Image", filter,
			                                               proposed.extension().wstring(),
			                                               proposed.filename().wstring()
		                                               });
		if (!chosen) return false;
		if (!save_to(*chosen, false))
		{
			report_save_error();
			return false;
		}
		// The original is left exactly as it was, and the new file becomes the edit target.
		saved_as(*chosen);
		return true;
	}

	bool TaskEdit::step_photo(const int direction, const bool saveFirst)
	{
		if (loading_ || saving_ || targets_.size() < 2) return false;
		finish_drag();
		if (dirty())
		{
			// Save changes target only after a successful write; Cancel keeps both.
			if (saveFirst)
			{
				if (!save()) return false;
			}
			else
				switch (ask_about_draft())
				{
				case Leaving::save:
					if (!save()) return false;
					break;
				case Leaving::cancel:
					return true;
				default:
					edits_ = {};
					write_controls();
					break;
				}
		}
		// A format conversion follows Save as semantics and leaves this task through the host.
		if (closingAfterSave_) return true;
		if (targets_.empty()) return false;
		const auto count = static_cast<int>(targets_.size());
		index_ = static_cast<size_t>(((static_cast<int>(index_) + direction) % count + count) % count);
		path_ = targets_[index_];
		edits_ = {};
		write_controls();
		load_photo();
		invalidate();
		return true;
	}

	bool TaskEdit::request_close()
	{
		if (saving_) return false;
		finish_drag();
		switch (ask_about_draft())
		{
		case Leaving::save: return save();
		case Leaving::cancel: return false;
		default: return true;
		}
	}
}
