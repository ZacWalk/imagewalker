// ImageWalker by Zac Walker
// Implements Convert or Resize: originals are kept and new files are written to a destination.

#include "TaskConvert.h"

#include "ImageResampler.h"
#include "Paths.h"

#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>
#include <windows.h>

namespace iw
{
	namespace
	{
		enum ControlId
		{
			idDescription = 1,
			idDestinationHeading,
			idDestination,
			idFormatHeading,
			idJpeg,
			idQuality,
			idPng,
			idTiff,
			idWebp,
			idWebpQuality,
			idLossless,
			idLimit,
			idLimitValue,
			idCollisionHeading,
			idCollision
		};

		constexpr int formatGroup = 1;

		bool is_webp(const files::ImageSaveFormat format)
		{
			return paths::iequals(files::image_save_extension(format), L".webp");
		}

		tasks::CollisionPolicy policy_from_choice(const int value)
		{
			switch (value)
			{
			case 1: return tasks::CollisionPolicy::skip;
			case 2: return tasks::CollisionPolicy::autoRename;
			case 3: return tasks::CollisionPolicy::replace;
			default: return tasks::CollisionPolicy::block;
			}
		}

	}

	sizei limited_output_size(const int width, const int height, const int limit)
	{
		if (limit <= 0 || width <= 0 || height <= 0) return {width, height};
		const int longest = (std::max)(width, height);
		// Only ever smaller: a photo already inside the limit is left alone.
		if (longest <= limit) return {width, height};
		const auto scale = static_cast<double>(limit) / longest;
		return {
			(std::max)(1, static_cast<int>(width * scale + 0.5)),
			(std::max)(1, static_cast<int>(height * scale + 0.5))
		};
	}

	void TaskConvert::set_initial_destination(std::filesystem::path folder)
	{
		destination_ = std::move(folder);
	}

	void TaskConvert::build_controls()
	{
		formats_ = files::writable_image_formats();
		std::erase(formats_, files::ImageSaveFormat::bmp);

		const auto changed = [this] { refresh_changes(); controls_changed(); };
		const auto redraw = [this] { refresh_changes(); };

		ui::Control description;
		description.id = idDescription;
		description.kind = ui::ControlKind::label;
		description.label =
			L"Your original photos are kept. A new file is written to the destination folder for each one.";
		panel_.add(std::move(description));

		ui::Control destinationHeading;
		destinationHeading.id = idDestinationHeading;
		destinationHeading.kind = ui::ControlKind::heading;
		destinationHeading.label = L"Destination";
		panel_.add(std::move(destinationHeading));

		ui::Control destination;
		destination.id = idDestination;
		destination.kind = ui::ControlKind::folder;
		destination.label = L"Destination folder";
		destination.text = destination_.wstring();
		destination.changed = [this]
		{
			if (const auto* control = panel_.find(idDestination)) destination_ = control->text;
			controls_changed();
		};
		panel_.add(std::move(destination));

		ui::Control formatHeading;
		formatHeading.id = idFormatHeading;
		formatHeading.kind = ui::ControlKind::heading;
		formatHeading.label = L"Output format";
		panel_.add(std::move(formatHeading));

		const auto hasFormat = [this](const files::ImageSaveFormat format)
		{
			return std::ranges::find(formats_, format) != formats_.end();
		};

		if (hasFormat(files::ImageSaveFormat::jpeg))
		{
			ui::Control jpeg;
			jpeg.id = idJpeg;
			jpeg.kind = ui::ControlKind::radio;
			jpeg.label = L"JPEG - Best compression for photos";
			jpeg.group = formatGroup;
			jpeg.checked = true;
			jpeg.changed = changed;
			panel_.add(std::move(jpeg));

			ui::Control quality;
			quality.id = idQuality;
			quality.kind = ui::ControlKind::slider;
			quality.label = L"Quality";
			quality.suffix = L"%";
			quality.minimum = 1;
			quality.maximum = 100;
			quality.value = 85;
			quality.indent = 1;
			quality.changed = redraw;
			panel_.add(std::move(quality));
		}

		if (hasFormat(files::ImageSaveFormat::png))
		{
			ui::Control png;
			png.id = idPng;
			png.kind = ui::ControlKind::radio;
			png.label = L"PNG - Best for non photos";
			png.group = formatGroup;
			png.checked = !panel_.find(idJpeg);
			png.changed = changed;
			panel_.add(std::move(png));
		}

		if (hasFormat(files::ImageSaveFormat::tiff))
		{
			ui::Control tiff;
			tiff.id = idTiff;
			tiff.kind = ui::ControlKind::radio;
			tiff.label = L"TIFF - Best for archiving";
			tiff.group = formatGroup;
			tiff.checked = !panel_.find(idJpeg) && !panel_.find(idPng);
			tiff.changed = changed;
			panel_.add(std::move(tiff));
		}

		if (std::ranges::any_of(formats_, is_webp))
		{
			ui::Control webp;
			webp.id = idWebp;
			webp.kind = ui::ControlKind::radio;
			webp.label = L"WEBP - Best for the web";
			webp.group = formatGroup;
			webp.checked = !panel_.find(idJpeg) && !panel_.find(idPng) && !panel_.find(idTiff);
			webp.changed = changed;
			panel_.add(std::move(webp));

			ui::Control quality;
			quality.id = idWebpQuality;
			quality.kind = ui::ControlKind::slider;
			quality.label = L"WebP quality";
			quality.suffix = L"%";
			quality.minimum = 1;
			quality.maximum = 100;
			quality.value = 85;
			quality.indent = 1;
			quality.changed = redraw;
			quality.enabled = panel_.find(idWebp)->checked;
			panel_.add(std::move(quality));

			ui::Control lossless;
			lossless.id = idLossless;
			lossless.kind = ui::ControlKind::checkBox;
			lossless.label = L"Lossless compression";
			lossless.indent = 1;
			lossless.changed = redraw;
			lossless.enabled = panel_.find(idWebp)->checked;
			panel_.add(std::move(lossless));
		}

		ui::Control limit;
		limit.id = idLimit;
		limit.kind = ui::ControlKind::checkBox;
		limit.label = L"Limit output to a maximum dimension";
		limit.changed = redraw;
		panel_.add(std::move(limit));

		ui::Control limitValue;
		limitValue.id = idLimitValue;
		limitValue.kind = ui::ControlKind::text;
		limitValue.label = L"Longest side (pixels)";
		limitValue.suffix = L" px";
		limitValue.minimum = 0;
		limitValue.maximum = 65535;
		limitValue.value = 1920;
		limitValue.text = L"1920";
		limitValue.indent = 1;
		limitValue.enabled = false;
		limitValue.changed = redraw;
		panel_.add(std::move(limitValue));

		ui::Control collisionHeading;
		collisionHeading.id = idCollisionHeading;
		collisionHeading.kind = ui::ControlKind::heading;
		collisionHeading.label = L"If the destination already exists";
		panel_.add(std::move(collisionHeading));

		ui::Control collision;
		collision.id = idCollision;
		collision.kind = ui::ControlKind::choice;
		collision.label = L"Existing files";
		collision.choices = {L"Block run", L"Skip", L"Auto-rename", L"Replace"};
		collision.value = 0;
		collision.changed = changed;
		panel_.add(std::move(collision));

		if (!limitInput_ && frame_)
		{
			platform::TextInputOptions options;
			options.text = L"1920";
			options.cueBanner = L"Maximum dimension in pixels";
			options.changed = [this](const std::wstring& text)
			{
				if (auto* control = panel_.find(idLimitValue)) control->text = text;
				refresh_changes();
			};
			bind_text_input(idLimitValue, options);
			limitInput_ = platform::create_text_input(frame_, std::move(options));
			if (limitInput_) limitInput_->set_font(platform::create_message_font(dpi_));
		}
	}

	std::vector<TaskView::Column> TaskConvert::review_columns() const
	{
		return {{L"Source", 3}, {L"Destination", 3}, {finished_ ? L"Status" : L"Changes", 3}};
	}

	convert::Settings TaskConvert::read_settings() const
	{
		convert::Settings settings;
		settings.destination = destination_;
		if (const auto* jpeg = panel_.find(idJpeg); jpeg && jpeg->checked)
			settings.format = files::ImageSaveFormat::jpeg;
		else if (const auto* png = panel_.find(idPng); png && png->checked)
			settings.format = files::ImageSaveFormat::png;
		else if (const auto* tiff = panel_.find(idTiff); tiff && tiff->checked)
			settings.format = files::ImageSaveFormat::tiff;
		else if (const auto* webp = panel_.find(idWebp); webp && webp->checked)
		{
			const auto format = std::ranges::find_if(formats_, is_webp);
			if (format != formats_.end()) settings.format = *format;
		}
		else if (!formats_.empty()) settings.format = formats_.front();
		if (const auto* quality = panel_.find(idQuality)) settings.quality = quality->value;
		if (is_webp(settings.format))
		{
			if (const auto* quality = panel_.find(idWebpQuality)) settings.quality = quality->value;
			if (const auto* lossless = panel_.find(idLossless)) settings.lossless = lossless->checked;
		}
		if (const auto* limit = panel_.find(idLimit); limit && limit->checked)
		{
			settings.limitEnabled = true;
			if (const auto* value = panel_.find(idLimitValue))
			{
				const auto text = limitInput_ ? limitInput_->text() : value->text;
				settings.maximumDimension = 0;
				for (const auto digit : text)
				{
					if (digit < L'0' || digit > L'9' ||
						settings.maximumDimension > ((std::numeric_limits<int>::max)() - (digit - L'0')) / 10)
					{
						settings.maximumDimension = -1;
						break;
					}
					settings.maximumDimension = settings.maximumDimension * 10 + digit - L'0';
				}
			}
		}
		if (const auto* collision = panel_.find(idCollision))
			settings.policy = policy_from_choice(collision->value);
		return settings;
	}

	tasks::TaskRunner::AnalyzeFunction TaskConvert::make_analyzer()
	{
		const auto settings = read_settings();
		auto sources = targets();
		return [settings, sources = std::move(sources)](const std::stop_token& stop,
		                                                const tasks::ProgressFunction& progress)
		{
			return convert::analyze(sources, settings, stop, progress);
		};
	}

	void TaskConvert::refresh_changes()
	{
		const auto settings = read_settings();
		panel_.set_enabled(idQuality, settings.format == files::ImageSaveFormat::jpeg);
		panel_.set_enabled(idWebpQuality, is_webp(settings.format) && !settings.lossless);
		panel_.set_enabled(idLossless, is_webp(settings.format));
		panel_.set_enabled(idLimitValue, settings.limitEnabled);
		refresh_commands();
		invalidate();
	}

	std::wstring TaskConvert::review_cell(const tasks::TaskRow& row, const size_t column) const
	{
		if (column == 0) return row.source.wstring();
		if (column == 1) return row.destination.wstring();
		if (column == 2 && !finished_ && (row.state == tasks::RowState::ready ||
			row.state == tasks::RowState::blocked || row.state == tasks::RowState::skipped))
			return convert::describe_changes(row, read_settings());
		return TaskView::review_cell(row, column);
	}

	std::wstring TaskConvert::run_block_reason() const
	{
		if (formats_.empty()) return L"No supported output image encoder is installed.";
		return convert::settings_problem(read_settings());
	}

	void TaskConvert::control_activated(ui::Control& control)
	{
		if (control.id == idLimitValue && limitInput_ && control.enabled)
		{
			limitInput_->set_focus();
			return;
		}
		TaskView::control_activated(control);
	}

	void TaskConvert::layout_hosted_controls()
	{
		if (!limitInput_) return;
		const auto bounds = panel_.text_bounds(idLimitValue);
		const bool visible = bounds.width > 0 && bounds.height > 0 && panel_.interactive() && read_settings().limitEnabled;
		limitInput_->show(visible);
		if (visible) limitInput_->set_bounds(bounds);
	}

	void TaskConvert::work_state_changed(const bool busy)
	{
		if (limitInput_ && busy) limitInput_->show(false);
		if (!busy) layout_hosted_controls();
	}

	tasks::RunOptions TaskConvert::build_run_options()
	{
		return convert::run_options(read_settings(), targets());
	}
}

namespace iw::convert
{
	namespace
	{
		struct Reviewed
		{
			int width{};
			int height{};
			std::filesystem::path parent;
			BY_HANDLE_FILE_INFORMATION source{};
			bool sourceKnown{};
			std::optional<files::FileSnapshot> destination;
		};

		bool file_information(const std::filesystem::path& path, BY_HANDLE_FILE_INFORMATION& info)
		{
			const HANDLE file = CreateFileW(files::native_path(path).c_str(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			const bool read = GetFileInformationByHandle(file, &info) != FALSE;
			CloseHandle(file);
			return read && !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
		}

		bool unchanged_source(const std::filesystem::path& path, const Reviewed& reviewed)
		{
			BY_HANDLE_FILE_INFORMATION current{};
			const auto& original = reviewed.source;
			return reviewed.sourceKnown && file_information(path, current) &&
				current.dwVolumeSerialNumber == original.dwVolumeSerialNumber &&
				current.nFileIndexHigh == original.nFileIndexHigh && current.nFileIndexLow == original.nFileIndexLow &&
				current.nFileSizeHigh == original.nFileSizeHigh && current.nFileSizeLow == original.nFileSizeLow &&
				current.ftLastWriteTime.dwHighDateTime == original.ftLastWriteTime.dwHighDateTime &&
				current.ftLastWriteTime.dwLowDateTime == original.ftLastWriteTime.dwLowDateTime;
		}

		std::filesystem::path resolved_path(const std::filesystem::path& path)
		{
			std::error_code error;
			const auto result = std::filesystem::weakly_canonical(files::native_path(path), error);
			if (error) throw std::runtime_error("The destination path cannot be accessed.");
			return result;
		}

		bool is_original(const std::filesystem::path& destination,
		                 const std::vector<std::filesystem::path>& originals)
		{
			const auto normalized = resolved_path(destination);
			for (const auto& source : originals)
			{
				if (paths::equal(normalized, resolved_path(source))) return true;
				std::error_code error;
				if (std::filesystem::equivalent(files::native_path(source), files::native_path(destination), error))
					return true;
			}
			return false;
		}
	}

	std::wstring settings_problem(const Settings& settings)
	{
		if (settings.destination.empty()) return L"Choose a destination folder.";
		if (settings.limitEnabled && settings.maximumDimension < 1)
			return L"The maximum dimension must be at least one pixel.";
		return {};
	}

	std::wstring describe_changes(const tasks::TaskRow& row, const Settings& settings)
	{
		if (row.state == tasks::RowState::blocked || row.state == tasks::RowState::skipped)
			return row.change;
		const auto reviewed = std::static_pointer_cast<const Reviewed>(row.context);
		const auto extension = files::image_save_extension(settings.format);
		auto result = std::format(L"Converted to {}", extension);
		if (reviewed && reviewed->width > 0 && reviewed->height > 0)
		{
			const auto size = limited_output_size(reviewed->width, reviewed->height,
			                                     settings.limitEnabled ? settings.maximumDimension : 0);
			result = std::format(L"{} x {} {}", size.width, size.height, extension);
		}
		if (settings.format == files::ImageSaveFormat::jpeg)
			result += std::format(L", quality {}%", settings.quality);
		if (is_webp(settings.format))
			result += settings.lossless ? L", lossless" : std::format(L", quality {}%", settings.quality);
		if (row.replace) result += L" - Replace";
		else if (row.change.starts_with(L"Renamed")) result += L" - " + row.change;
		return result;
	}

	tasks::TaskPlan analyze(const std::vector<std::filesystem::path>& sources, const Settings& settings,
	                       const std::stop_token& stop, const tasks::ProgressFunction& progress)
	{
		tasks::TaskPlan plan;
		if (settings.destination.empty())
		{
			plan.blockReason = settings_problem(settings);
			return plan;
		}
		// Dimensions are an execution option, not part of destination occupancy. The view supplies
		// that blocker separately so correcting an invalid value can reuse this exact snapshot.
		std::error_code error;
		const auto parent = resolved_path(settings.destination);
		if (std::filesystem::exists(parent, error) && !std::filesystem::is_directory(parent, error))
		{
			plan.blockReason = L"The destination must be a folder.";
			return plan;
		}
		if (error)
		{
			plan.blockReason = L"The destination folder cannot be accessed.";
			return plan;
		}
		plan.rows.reserve(sources.size());
		for (const auto& source : sources)
		{
			if (stop.stop_requested()) break;
			tasks::TaskRow row;
			row.source = source;
			row.destination = settings.destination / (source.stem().wstring() + files::image_save_extension(settings.format));
			error.clear();
			if (!files::is_supported_image(source) || !std::filesystem::is_regular_file(files::native_path(source), error))
			{
				row.state = tasks::RowState::blocked;
				row.change = L"Select only local photo files that can be read.";
			}
			auto reviewed = std::make_shared<Reviewed>();
			reviewed->parent = parent;
			reviewed->sourceKnown = file_information(source, reviewed->source);
			if (!reviewed->sourceKnown)
			{
				row.state = tasks::RowState::blocked;
				row.change = L"The source photo cannot be read as a local regular file.";
			}
			files::read_dimensions(source, reviewed->width, reviewed->height);
			if (reviewed->sourceKnown && !unchanged_source(source, *reviewed))
			{
				row.state = tasks::RowState::blocked;
				row.change = L"The source changed during analysis. Refresh to review it again.";
			}
			row.context = std::move(reviewed);
			plan.rows.push_back(std::move(row));
			if (progress) progress(plan.rows.size(), sources.size());
		}
		tasks::resolve_collisions(plan, settings.policy,
			[](const std::filesystem::path& path)
			{
				std::error_code ec;
				const auto status = std::filesystem::symlink_status(files::native_path(path), ec);
				return (ec && ec != std::errc::no_such_file_or_directory) || std::filesystem::exists(status);
			},
			[](const std::filesystem::path& proposed, const tasks::ExistsFunction& taken)
			{
				return files::unique_destination(proposed, taken);
			});
		for (auto& row : plan.rows)
		{
			if (row.state != tasks::RowState::ready) continue;
			auto reviewed = std::make_shared<Reviewed>(*std::static_pointer_cast<const Reviewed>(row.context));
			reviewed->destination = files::snapshot_file(row.destination);
			row.context = reviewed;
			if (!reviewed->destination || reviewed->destination->exists != row.replace)
			{
				row.state = tasks::RowState::blocked;
				row.change = L"The destination changed during analysis or cannot be accessed. Refresh to review it again.";
				plan.blockReason = row.change;
				continue;
			}
			if (is_original(row.destination, sources))
			{
				row.state = tasks::RowState::blocked;
				row.change = L"An original photo cannot be replaced. Choose another folder or Auto-rename.";
				plan.blockReason = row.change;
			}
			else if (row.replace)
			{
				const auto attributes = GetFileAttributesW(files::native_path(row.destination).c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES ||
					(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_REPARSE_POINT)))
				{
					row.state = tasks::RowState::blocked;
					row.change = L"The replacement destination is not a writable regular file.";
					plan.blockReason = row.change;
				}
			}
		}
		return plan;
	}

	tasks::RunOptions run_options(const Settings& settings,
	                             const std::vector<std::filesystem::path>& originals)
	{
		tasks::RunOptions options;
		options.undoLabel = L"Convert or Resize";
		options.undoPaths = [](const tasks::TaskRow& row)
		{ return std::vector<std::filesystem::path>{row.destination}; };
		options.revalidate = [settings, originals](tasks::TaskRow& row)
		{
			if (const auto reason = settings_problem(settings); !reason.empty())
			{
				row.detail = reason;
				return false;
			}
			if (is_original(row.destination, originals))
			{
				row.detail = L"An original photo cannot be replaced.";
				return false;
			}
			const auto reviewed = std::static_pointer_cast<const Reviewed>(row.context);
			if (reviewed && !unchanged_source(row.source, *reviewed))
			{
				row.detail = L"The source photo changed after Review. Analyze again.";
				return false;
			}
			if (row.replace && (!reviewed || !reviewed->destination ||
				!files::matches_snapshot(row.destination, *reviewed->destination)))
			{
				row.detail = L"The approved replacement changed after Review. Analyze again.";
				return false;
			}
			if (reviewed && !paths::equal(resolved_path(row.destination.parent_path()), reviewed->parent))
			{
				row.detail = L"The destination folder changed after Review. Analyze again.";
				return false;
			}
			std::error_code error;
			if (!std::filesystem::is_regular_file(files::native_path(row.source), error) || error)
			{
				row.detail = L"The source photo is no longer a readable local file. Analyze again.";
				return false;
			}
			return true;
		};
		options.act = [settings, originals](tasks::TaskRow& row)
		{
			std::error_code error;
			// A destination reviewed as free is checked again: an unreviewed file is never replaced.
			if (!row.replace && std::filesystem::exists(files::native_path(row.destination), error))
			{
				row.state = tasks::RowState::skipped;
				row.change = L"Skipped - the destination appeared after review";
				return;
			}
			error.clear();
			std::filesystem::create_directories(files::native_path(row.destination.parent_path()), error);
			if (error) throw std::runtime_error("The destination folder could not be created.");
			auto image = files::load_image(row.source);
			if (image.width <= 0 || image.height <= 0 || image.pixels.empty())
				throw std::runtime_error("The photo could not be decoded.");
			const auto reviewed = std::static_pointer_cast<const Reviewed>(row.context);
			if (reviewed && !unchanged_source(row.source, *reviewed))
				throw std::runtime_error("The source photo changed while it was being decoded. Analyze again.");
			const auto size = limited_output_size(image.width, image.height,
			                                     settings.limitEnabled ? settings.maximumDimension : 0);
			if (size.width != image.width || size.height != image.height)
			{
				auto scaled = ui::resample_bgra(image.pixels, {image.width, image.height},
				                                {0.0, 0.0, static_cast<double>(image.width),
				                                 static_cast<double>(image.height)}, size);
				if (scaled.empty()) throw std::runtime_error("The photo could not be resized.");
				image.pixels = std::move(scaled);
				image.width = size.width;
				image.height = size.height;
			}
			if (is_original(row.destination, originals) ||
				(reviewed && !paths::equal(resolved_path(row.destination.parent_path()), reviewed->parent)))
				throw std::runtime_error("The destination changed while preparing the output. Analyze again.");
			if (!files::save_image(image, row.destination, settings.format,
			                       {settings.quality, settings.lossless}, row.replace, [&]
			                       {
				                       if (is_original(row.destination, originals) ||
					                       (reviewed && (!unchanged_source(row.source, *reviewed) ||
						                       !paths::equal(resolved_path(row.destination.parent_path()), reviewed->parent))))
					                       throw std::runtime_error("The source or destination changed before the output was committed. Analyze again.");
				                       if (row.replace && (!reviewed || !reviewed->destination ||
					                       !files::matches_snapshot(row.destination, *reviewed->destination)))
					                       throw std::runtime_error("The approved replacement changed before the output was committed. Analyze again.");
				                       return true;
			                       }))
			{
				error.clear();
				if (!row.replace && std::filesystem::exists(files::native_path(row.destination), error))
				{
					row.state = tasks::RowState::skipped;
					row.change = L"Skipped - the destination appeared after review";
					return;
				}
				throw std::runtime_error("The new file could not be written.");
			}
			row.change = std::format(L"{} x {}", image.width, image.height);
		};
		return options;
	}
}
