// ImageWalker by Zac Walker

#include "AgentSession.h"
#include "MainWindow.h"
#include "TaskSync.h"

#include <libfiles/encode.h>

#include <algorithm>
#include <format>

namespace iw
{
	namespace
	{
		namespace mcp = ::core::mcp;
		using mcp::tool_result;

		std::string filename(const std::filesystem::path& path)
		{
			return ::core::utf16_to_utf8(path.filename().native());
		}

		mcp::value json_string(const std::string_view text, rapidjson::Document::AllocatorType& allocator)
		{
			return mcp::value(text.empty() ? "" : text.data(), static_cast<rapidjson::SizeType>(text.size()),
			                  allocator);
		}

		std::string_view scan_state(const LoadState state)
		{
			switch (state)
			{
			case LoadState::empty: return "empty";
			case LoadState::loading: return "loading";
			case LoadState::ready: return "ready";
			case LoadState::failed: return "failed";
			}
			return {};
		}
	}

	// No forwarding of input, close commands or persistence messages to the interactive root reactor.
	class AgentSession::Frame final : public platform::FrameReactor
	{
	public:
		explicit Frame(const std::shared_ptr<MainWindow>& window) : window_(window) {}

		platform::MessageResult message(const platform::WindowFramePtr& frame,
		                                const platform::WindowMessage message) override
		{
			if (message == platform::WindowMessage::dpiChanged)
				if (const auto window = window_.lock()) window->update_dpi(frame->dpi());
			return 0;
		}

		void size(const platform::WindowFramePtr&, app::sizei, platform::MeasureContext&) override
		{
			if (const auto window = window_.lock()) window->on_size();
		}

	private:
		std::weak_ptr<MainWindow> window_;
	};

	AgentSession::AgentSession(std::unique_ptr<platform::agent::Fixture> fixture, const bool captureAllowed) :
		fixture_(std::move(fixture)),
		server_(std::make_shared<mcp::server>(
			mcp::server_info{"ImageWalker 3.0", "3.0.0",
	                         "Read-only UI testing on the fixture set at startup. Poll ui_state after changes. "
	                         "No live collection, file writes, launching or generic UI input. Close stdin to exit."})),
		captureAllowed_(captureAllowed)
	{
		const auto add =
			[this](const char* name, const char* description, const bool readOnly, const char* schema = nullptr)
		{
			mcp::tool tool;
			tool.name = name;
			tool.description = description;
			tool.annotations = {.read_only = readOnly, .idempotent = readOnly};
			if (schema) tool.input_schema = schema;
			tool.handler = [this](const mcp::call_ptr& call)
			{
				if (call->is_cancelled()) return;
				if (const auto window = window_.lock()) dispatch(*window, call);
				else call->finish(tool_result::error("The fixture window has closed."));
			};
			if (!server_->add_tool(std::move(tool))) throw std::logic_error("Invalid ImageWalker MCP registration");
		};
		add("ui_state", "Read the relative folder, visible items, selection, scan and thumbnail counts, and view.",
		    true);
		add("browse", "Browse an admitted folder by its exact relative key (use . for the root and / between names).",
		    false,
		    R"({"type":"object","properties":{"folder":{"type":"string","minLength":1,"maxLength":512}},)"
		    R"("required":["folder"],"additionalProperties":false})");
		add("select_photos", "Select and preview 1–16 distinct photo names from the current folder.", false,
		    R"({"type":"object","properties":{"names":{"type":"array","minItems":1,"maxItems":16,)"
		    R"("items":{"type":"string","minLength":1,"maxLength":255}}},"required":["names"],"additionalProperties":false})");
		add("open_preview", "Open a task's read-only preview. Sync opens unconfigured and is not analyzed.", false,
		    R"({"type":"object","properties":{"view":{"enum":["convert","rename","edit","sync"]}},)"
		    R"("required":["view"],"additionalProperties":false})");
		add("close_view", "Return to Browse from a task preview or fullscreen.", false);
		add("set_fullscreen", "Enter or leave fullscreen within the cloaked fixture window.", false,
		    R"({"type":"object","properties":{"enabled":{"type":"boolean"}},"required":["enabled"],)"
		    R"("additionalProperties":false})");
		if (captureAllowed_)
			add("screenshot",
			    "Return an inline PNG of the fixture window at its current state. Poll ui_state first for "
			    "scan, thumbnail, and selected-photo readiness. No output path.",
			    true,
			    R"({"type":"object","properties":{"max_edge":{"type":"integer","minimum":128,"maximum":896}},)"
			    R"("additionalProperties":false})");
	}

	bool AgentSession::start(const std::shared_ptr<MainWindow>& window)
	{
		window_ = window;
		window->showTree_ = false;
		window->collection_.set_photos_only(false);
		platform::WindowOptions options;
		options.className = "ImageWalker30.Agent";
		options.title = L"ImageWalker 3.0";
		options.useDefaultPosition = true;
		window->frame_ = platform::create_top_level_frame({}, options);
		if (!window->frame_) return false;

		// Use the same chrome, mode bar, canvas and task views, but never start interactive discovery
		// (shell, saved folders, tools, drop targets) or restore a saved location.
		window->update_dpi(window->frame_->dpi());
		window->chrome_ = platform::create_command_surface(window->frame_);
		if (!window->chrome_) return false;
		window->configure_chrome();
		options = {};
		options.child = true;
		options.eraseBackground = false;
		window->modeBar_ = std::make_shared<ModeBar>();
		if (!window->modeBar_->create(window->frame_) || !window->canvas_.create(window->frame_)) return false;

		window->canvas_.set_dpi(window->dpi_);
		window->canvas_.set_selection_handler([weak = window_]
		{
			if (const auto w = weak.lock()) w->update_selection_status();
		});
		window->canvas_.set_progress_handler([this, weak = window_](const size_t complete, const size_t total)
		{
			thumbnailsComplete_ = complete;
			thumbnailsTotal_ = total;
			if (const auto w = weak.lock(); w && !w->task_ && w->collection_.load_state() != LoadState::loading)
				w->update_progress(complete, total);
		});
		window->canvas_.set_load_error_handler([this](const std::filesystem::path&) { imageFailed_ = true; });
		window->set_thumbnail_size(window->thumbnailSize_);
		window->set_view_mode(window->viewMode_);
		window->canvas_.set_fit(window->fitImage_);
		window->configure_mode_bar();
		window->frame_->set_reactor(std::make_shared<Frame>(window));
		window->on_size();
		return browse(*window, ".");
	}

	void AgentSession::stop()
	{
		const auto window = window_.lock();
		if (!window) return;
		if (window->task_ && window->task_->frame()) window->task_->frame()->set_reactor({});
		if (window->modeBar_ && window->modeBar_->frame()) window->modeBar_->frame()->set_reactor({});
		if (window->canvas_.frame()) window->canvas_.frame()->set_reactor({});
		if (window->frame_) window->frame_->set_reactor({});
		window_.reset();
	}

	void AgentSession::dispatch(MainWindow& window, const mcp::call_ptr& call)
	{
		const auto& tool = call->tool();
		if (tool == "ui_state") return call->finish(state(window));
		if (tool == "screenshot" && captureAllowed_) return screenshot(window, call);
		if (tool == "select_photos") return call->finish(select_photos(window, call->texts("names")));
		if (tool == "open_preview") return call->finish(open_preview(window, call->text("view")));
		if (tool == "close_view") return call->finish(close_view(window));
		if (tool == "set_fullscreen") return call->finish(fullscreen(window, call->flag("enabled", false)));
		if (tool != "browse") return call->finish(tool_result::error("Unsupported action."));
		if (window.task_ || window.fullscreen_)
			return call->finish(tool_result::error("Close the current view before browsing."));
		const auto key = call->text("folder");
		if (!fixture_->find(key)) return call->finish(tool_result::error("Folder is not in the admitted fixture."));
		call->finish(browse(window, key) ? state(window) : tool_result::error("Could not queue the fixture scan."));
	}

	tool_result AgentSession::state(const MainWindow& window) const
	{
		std::string_view view = window.fullscreen_ ? "fullscreen" : "browse";
		if (window.task_) switch (window.task_->mode_id())
			{
			case MainWindow::modeConvert: view = "convert"; break;
			case MainWindow::modeRename: view = "rename"; break;
			case MainWindow::modeEdit: view = "edit"; break;
			case MainWindow::modeSync: view = "sync"; break;
			default: view = "unknown"; break;
			}
		rapidjson::Document doc(rapidjson::kObjectType);
		auto& allocator = doc.GetAllocator();
		const auto text = [&](const std::string_view value)
		{
			return json_string(value, allocator);
		};
		mcp::value items(rapidjson::kArrayType);
		for (const auto& item : window.collection_.visible_items())
		{
			mcp::value entry(rapidjson::kObjectType);
			entry.AddMember("name", text(filename(item.path)), allocator);
			entry.AddMember("kind", text(item.kind == files::ItemKind::folder ? "folder" : "photo"), allocator);
			items.PushBack(std::move(entry), allocator);
		}
		mcp::value selected(rapidjson::kArrayType);
		for (const auto& path : window.canvas_.selected_paths()) selected.PushBack(text(filename(path)), allocator);

		mcp::value scan(rapidjson::kObjectType);
		scan.AddMember("state", text(scan_state(window.collection_.load_state())), allocator);
		scan.AddMember("complete", scanComplete_, allocator);
		scan.AddMember("total", scanTotal_, allocator);
		scan.AddMember("photos", scanPhotos_, allocator);
		mcp::value thumbnails(rapidjson::kObjectType);
		thumbnails.AddMember("complete", thumbnailsComplete_, allocator);
		thumbnails.AddMember("total", thumbnailsTotal_, allocator);
		const auto& photo = window.canvas_.path();
		const auto photoFolder = photo.empty() ? std::filesystem::path{}
		                                       : photo.parent_path().lexically_relative(fixture_->find(".")->path);
		const auto imageState = photo.empty()                          ? "none"
		                        : imageFailed_                         ? "failed"
		                        : window.canvas_.has_displayed_image() ? "ready"
		                                                               : "loading";

		doc.AddMember("folder", text(folder_), allocator);
		doc.AddMember("view", text(view), allocator);
		doc.AddMember("scan", std::move(scan), allocator);
		doc.AddMember("thumbnails", std::move(thumbnails), allocator);
		doc.AddMember("items", std::move(items), allocator);
		doc.AddMember("selected", std::move(selected), allocator);
		doc.AddMember("photo", text(filename(photo)), allocator);
		doc.AddMember("photo_folder", text(::core::utf16_to_utf8(photoFolder.generic_wstring())), allocator);
		doc.AddMember("image_state", text(imageState), allocator);
		doc.AddMember("task_busy", window.task_ && window.task_->busy(), allocator);
		doc.AddMember("review_rows", window.task_ ? window.task_->plan().rows.size() : 0, allocator);
		doc.AddMember("capture_allowed", captureAllowed_, allocator);
		doc.AddMember("status", text(::core::utf16_to_utf8(window.statusText_)), allocator);
		auto result = tool_result::text(mcp::to_json(doc));
		result.structured = result.content.front().text;
		return result;
	}

	bool AgentSession::browse(MainWindow& window, std::string key)
	{
		const auto* fixtureFolder = fixture_->find(key);
		if (!fixtureFolder) return false;
		auto items = fixtureFolder->items;
		folder_ = std::move(key);
		window.folder_ = fixtureFolder->path;
		window.files_.clear();
		window.collection_.clear();
		window.canvas_.set_files({});
		window.collection_.begin_load();
		scanComplete_ = scanPhotos_ = thumbnailsComplete_ = thumbnailsTotal_ = 0;
		scanTotal_ = items.size();
		imageFailed_ = false;
		window.frame_->set_title(std::format(L"{} - ImageWalker 3.0", window.folder_.filename().wstring()));
		window.chrome_->set_breadcrumbs({{window.folder_, window.folder_.filename().wstring()}}, [](const auto&) {});
		window.statusText_ = L"Scanning fixture...";
		window.scanning_ = true;
		window.update_status_surface();
		window.update_mode_bar();
		window.update_toolbar_state();

		// Use normal collection completion and thumbnail scheduling, but scan only the admitted entries,
		// never fresh directory contents. Switching folder invalidates queued progress and results.
		const auto weak = window_;
		const auto generation = ++window.folderScanGeneration_;
		const auto queued = platform::queue_work(platform::WorkQueue::folder,
		                                         [this, weak, generation, folder = window.folder_,
		                                          items = std::move(items)]() mutable
		{
			const auto exiting = platform::exit_token();
			size_t entries = 0;
			size_t photos = 0;
			for (auto& item : items)
			{
				const auto w = weak.lock();
				if (!w || generation != w->folderScanGeneration_ || exiting.stop_requested()) return;
				std::error_code error;
				item.modified = std::filesystem::last_write_time(item.path, error);
				item.hasModified = !error;
				++entries;
				if (item.kind == files::ItemKind::image) ++photos;
				if (entries % 16 != 0 && entries != items.size()) continue;
				platform::queue_ui([this, weak, generation, entries, photos]
				{
					const auto w = weak.lock();
					if (!w || generation != w->folderScanGeneration_) return;
					scanComplete_ = entries;
					scanPhotos_ = photos;
					w->report_scan_progress(generation, entries, photos);
				});
			}
			platform::queue_ui([weak, generation, folder = std::move(folder), items = std::move(items)]() mutable
			{
				if (const auto w = weak.lock()) w->finish_folder_scan(generation, folder, std::move(items), {});
			});
		});
		if (!queued)
			window.finish_folder_scan(generation, window.folder_, {},
			                          std::make_error_code(std::errc::operation_canceled));
		return queued;
	}

	tool_result AgentSession::select_photos(MainWindow& window, const std::vector<std::string>& names)
	{
		if (window.task_ || window.fullscreen_ || window.collection_.load_state() != LoadState::ready)
			return tool_result::error("Select photos from Browse after the fixture scan.");
		if (names.empty() || names.size() > 16) return tool_result::error("Supply 1–16 distinct photo names.");
		const auto& items = window.collection_.visible_items();
		std::vector<int> indices;
		for (const auto& name : names)
		{
			const auto it = std::ranges::find_if(items, [&name](const auto& item)
			{ return item.kind == files::ItemKind::image && filename(item.path) == name; });
			if (it == items.end()) return tool_result::error("Select only exact photo names in the current folder.");
			const auto index = static_cast<int>(it - items.begin());
			if (std::ranges::find(indices, index) != indices.end()) return tool_result::error("Duplicate photo name.");
			indices.push_back(index);
		}
		for (size_t i = 0; i < indices.size(); ++i) window.canvas_.select_index(indices[i], false, i != 0);
		imageFailed_ = false;
		window.canvas_.load(items[indices.back()].path);
		return state(window);
	}

	tool_result AgentSession::open_preview(MainWindow& window, const std::string_view view)
	{
		if (window.task_ || window.fullscreen_ || window.collection_.load_state() != LoadState::ready)
			return tool_result::error("Open a task from Browse after the fixture scan.");
		if (view == "sync")
		{
			// A blank section neither reads nor writes Sync's installed collection or remote scope.
			auto task = std::make_shared<TaskSync>(L"");
			task->set_mode_id(MainWindow::modeSync);
			task->set_local_folder(window.folder_);
			window.set_task(std::move(task));
		}
		else
		{
			if (!window.canvas_.selection_count())
				return tool_result::error("Select at least one fixture photo first.");
			if (view == "convert") window.open_convert_task();
			else if (view == "rename") window.open_rename_task();
			else if (view == "edit" && !window.editable_item().empty()) window.open_edit_task();
			else return tool_result::error("Unsupported task or photo.");
		}
		return window.task_ ? state(window) : tool_result::error("Could not open the task preview.");
	}

	tool_result AgentSession::close_view(MainWindow& window) const
	{
		if (window.task_ && (window.task_->busy() || !window.close_task()))
			return tool_result::error("The task is not ready to close. Poll ui_state.");
		if (window.fullscreen_) window.set_fullscreen(false);
		return state(window);
	}

	tool_result AgentSession::fullscreen(MainWindow& window, const bool enabled) const
	{
		if (window.task_) return tool_result::error("Close the task preview before entering fullscreen.");
		if (enabled && (imageFailed_ || window.canvas_.path().empty() || window.canvas_.selection_count() != 1 ||
		                !window.canvas_.has_displayed_image()))
			return tool_result::error("Select a photo and wait for it to load before entering fullscreen.");
		window.set_fullscreen(enabled);
		return state(window);
	}

	void AgentSession::screenshot(const MainWindow& window, const mcp::call_ptr& call)
	{
		const auto frame = platform::native_ui_frame(window.frame_);
		if (!frame) return call->finish(tool_result::error("The fixture frame is not available."));
		auto capture = platform::agent::capture_window(frame->handle(),
		                                               static_cast<int>(call->number("max_edge").value_or(896)));
		if (capture.pixels.empty())
			return call->finish(tool_result::error("The fixture window could not be captured offscreen."));
		const auto queued = platform::queue_work(platform::WorkQueue::image, [call, capture = std::move(capture)]
		{
			if (call->is_cancelled()) return;
			try
			{
				const auto png = libfiles::encode_png({reinterpret_cast<const std::uint8_t*>(capture.pixels.data()),
				                                       static_cast<size_t>(capture.width) * 4,
				                                       static_cast<std::uint32_t>(capture.width),
				                                       static_cast<std::uint32_t>(capture.height)},
				                                      1, {});
				if (!png.data.empty() && png.data.size() <= 2'600'000)
				{
					auto result = tool_result::text(std::format("{}x{} fixture window", capture.width, capture.height));
					result.structured = std::format(R"({{"width":{},"height":{}}})", capture.width, capture.height);
					mcp::content image;
					image.kind = mcp::content::kind_t::image;
					image.data.assign(reinterpret_cast<const char*>(png.data.data()), png.data.size());
					image.mime_type = "image/png";
					result.content.push_back(std::move(image));
					return call->finish(std::move(result));
				}
			}
			catch (const std::exception&)
			{
			}
			call->finish(tool_result::error("Could not encode a PNG within the capture limit."));
		});
		if (!queued) call->finish(tool_result::error("Could not queue PNG encoding."));
	}
}
