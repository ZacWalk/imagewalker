// ImageWalker by Zac Walker
// Implements ImageWalker 3.0 backend, workflow and native-platform regression tests.

#include "PlatformWin32.h"
#include <objbase.h>
#include "Platform.h"
#include "Tests.h"

#include "util_color.h"
#include "util_layout.h"
#include "CollectionState.h"
#include "Dpi.h"
#include "NavigationHistory.h"
#include "Paths.h"
#include "PixelOps.h"
#include "SelectionModel.h"
#include "Files.h"
#include "ControlPanel.h"
#include "ImageEdits.h"
#include "ImageResampler.h"
#include "LoadingModel.h"
#include "TaskConvert.h"
#include "TaskEdit.h"
#include "TaskModel.h"
#include "TaskRename.h"
#include "TaskRunner.h"
#include "TaskSync.h"
#include "Tools.h"
#include "ZoomModel.h"
#include "TestUiPlatform.h"
#include "DiagnosticLog.h"
#include "TestPhotoGeometry.h"
#include "UndoTests.h"
#include "MediaTests.h"

#include <array>
#include <filesystem>
#include <atomic>
#include <cmath>
#include <format>
#include <fstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace iw::tests
{
	namespace
	{
		void emit(const std::wstring& text)
		{
			platform::write_diagnostic(text);
		}

		class Reporter
		{
		public:
			void section(const wchar_t* name) { emit(std::format(L"[ RUN  ] {}\n", name)); }

			void check(const bool condition, const wchar_t* message)
			{
				++total_;
				if (condition) return;
				++failures_;
				emit(std::format(L"  FAIL: {}\n", message));
			}

			int failures() const { return failures_; }
			int total() const { return total_; }

		private:
			int failures_{};
			int total_{};
		};

		ui::Element make_item(const int id, const recti bounds)
		{
			ui::Element element;
			element.id = id;
			element.bounds = bounds;
			return element;
		}

		bool rect_equals(const recti a, const recti b)
		{
			return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
		}

		void test_mode_bar(Reporter& reporter)
		{
			reporter.section(L"ModeBar input, toggles and layout");
			const auto parent = std::make_shared<RecordingFrame>();
			struct Cleanup
			{
				std::shared_ptr<RecordingFrame> root;
				~Cleanup() { root->release_tree(); }
			} cleanup{parent};
			const auto bar = std::make_shared<ModeBar>();
			reporter.check(!bar->create({}), L"a mode bar refuses a missing parent");
			reporter.check(bar->create(parent), L"a mode bar creates its own child frame");
			const auto frame = MainWindowAccess::record(bar->frame());
			frame->move({0, 0, 68, 460});
			bar->set_modes({
				{1, ModeBar::Glyph::browse, L"Browse", {}, true},
				{2, ModeBar::Glyph::folders, L"Folders", {}, true},
				{3, ModeBar::Glyph::convert, L"Convert"},
				{4, ModeBar::Glyph::rename, L"Rename"},
				{5, ModeBar::Glyph::fullscreen, L"Full", {}, true}
			});
			bar->set_shortcuts({
				{100, ModeBar::Glyph::folder, L"First", L"first", false, true, true},
				{101, ModeBar::Glyph::pin, L"Pin"},
				{102, ModeBar::Glyph::folder, L"Clipped", L"clipped", false, true, true}
			});
			std::vector<ModeBar::Button> commands;
			std::vector<int> removed;
			bar->set_command_handler([&commands](const auto& button) { commands.push_back(button); });
			bar->set_remove_handler([&removed](const auto& button) { removed.push_back(button.id); });
			bar->set_checked(3);
			for (int row = 0; row < 5; ++row)
				frame->mouse(platform::MouseMessage::leftButtonDown, {34, 34 + 64 * row});
			reporter.check(commands.size() == 5 && !commands[0].checked && commands[1].checked &&
				commands[2].checked && !commands[3].checked && commands[4].checked,
				L"checking a mode clears other modes but retains Folders and Full toggles");
			commands.clear();
			bar->set_enabled(3, false);
			frame->mouse(platform::MouseMessage::leftButtonDown, {34, 162});
			frame->mouse(platform::MouseMessage::leftButtonDoubleClick, {34, 162});
			frame->mouse(platform::MouseMessage::leftButtonDown, {0, 34});
			frame->mouse(platform::MouseMessage::leftButtonDown, {34, 1});
			frame->mouse(platform::MouseMessage::leftButtonDown, {34, 500});
			reporter.check(commands.empty(), L"disabled buttons, margins and clipped shortcuts never dispatch");
			frame->mouse(platform::MouseMessage::contextMenu, {34, 363});
			frame->mouse(platform::MouseMessage::contextMenu, {34, 427});
			frame->mouse(platform::MouseMessage::contextMenu, {34, 34});
			reporter.check(removed == std::vector<int>{100}, L"only removable shortcuts dispatch removal");
			frame->mouse(platform::MouseMessage::move, {34, 34});
			const int invalidations = frame->invalidations;
			frame->mouse(platform::MouseMessage::move, {35, 35});
			reporter.check(frame->tracks == 1 && frame->invalidations == invalidations,
				L"moving inside the same button does not repaint or rearm leave tracking");
			frame->mouse(platform::MouseMessage::leave, {});
			frame->mouse(platform::MouseMessage::move, {34, 34});
			reporter.check(frame->tracks == 2 && frame->invalidations > invalidations,
				L"leaving clears hover and reentry rearms tracking");

			bar->set_dpi(192);
			reporter.check(bar->width() == 136, L"the strip width scales at 200 percent DPI");
			frame->move({0, 0, bar->width(), 920});
			bar->set_dpi(192);
			frame->mouse(platform::MouseMessage::leftButtonDown, {68, 68});
			reporter.check(commands.size() == 1 && commands.front().id == 1,
				L"hit testing uses the scaled layout");
			bar->set_dpi(0);
			reporter.check(bar->width() == 68, L"an unspecified DPI uses 96");

			bool stableArgument = false;
			bar->set_command_handler([&](const ModeBar::Button& button)
			{
				bar->set_modes({{99, ModeBar::Glyph::sync, L"Replacement"}});
				stableArgument = button.id == 1 && button.caption == L"Browse";
			});
			frame->mouse(platform::MouseMessage::leftButtonDown, {68, 34});
			reporter.check(stableArgument, L"a callback can rebuild the modes without invalidating its selected button");
		}

		class HostedTestTask final : public TaskView
		{
		public:
			explicit HostedTestTask(tasks::TaskRunner::Dispatcher dispatcher) : TaskView(std::move(dispatcher)) {}
			std::wstring title() const override { return L"Host integration"; }
			bool confirmClose{};
			bool undoAvailable{};
			int undoCount{};
			int prompts{};
			bool can_undo_draft() const override { return undoAvailable; }
			bool undo_draft() override
			{
				if (!undoAvailable) return false;
				++undoCount;
				undoAvailable = false;
				return true;
			}
			void publish_status(std::wstring text) { set_status(std::move(text)); }
		protected:
			void build_controls() override {}
			std::vector<Column> review_columns() const override { return {{L"Source"}}; }
			bool auto_analyze() const override { return false; }
			bool confirm_cancel_close() override { ++prompts; return confirmClose; }
			tasks::TaskRunner::AnalyzeFunction make_analyzer() override
			{
				return [](const std::stop_token&, const tasks::ProgressFunction& progress)
				{
					progress(1, 4);
					return tasks::TaskPlan{};
				};
			}
			tasks::RunOptions build_run_options() override { return {}; }
		};

		void test_main_window_host(Reporter& reporter)
		{
			reporter.section(L"MainWindow task switching and fullscreen host integration");
			std::vector<std::function<void()>> background, uiQueue;
			tasks::TaskRunner::Dispatcher dispatcher;
			dispatcher.background = [&background](std::function<void()> work)
			{
				background.push_back(std::move(work));
				return true;
			};
			dispatcher.ui = [&uiQueue](std::function<void()> work)
			{
				uiQueue.push_back(std::move(work));
				return true;
			};
			const auto drain = [](auto& queue)
			{
				while (!queue.empty())
				{
					auto work = std::move(queue.front());
					queue.erase(queue.begin());
					work();
				}
			};
			MainWindowAccess host;
			reporter.check(host.create(), L"the host attaches real canvas and mode-bar behavior to recording frames");
			reporter.check(host.tree->visible && host.splitter()->visible && host.modes()->visible,
				L"browsing initially shows the folder tree, splitter and modes");
			host.folders();
			reporter.check(!host.tree->visible && !host.splitter()->visible,
				L"the Folders mode command hides both tree and splitter");
			host.folders();
			const auto originalBounds = host.center()->bounds;
			auto task = std::make_shared<HostedTestTask>(dispatcher);
			task->set_mode_id(3);
			reporter.check(host.set_task(task), L"MainWindow accepts an idle task");
			reporter.check(!host.can_undo(), L"an ordinary task cannot invoke the global file history");
			task->undoAvailable = true;
			reporter.check(host.can_undo(), L"a task can expose its reversible draft to the host");
			host.undo();
			reporter.check(task->undoCount == 1 && host.task() == task && !host.can_undo(),
				L"host Undo routes to the task draft without closing it or changing file history");
			const auto taskFrame = MainWindowAccess::record(task->frame());
			reporter.check(!host.canvas()->visible && !host.tree->visible && !host.splitter()->visible &&
				taskFrame->visible && taskFrame->focused && host.modes()->visible,
				L"task entry replaces Items, focuses the task and retains the mode bar");
			reporter.check(host.root->title == L"Host integration - ImageWalker 3.0" &&
				host.center()->bounds.width > originalBounds.width &&
				rect_equals(taskFrame->bounds, host.center()->client_rect()),
				L"task entry updates the title and expands into the former tree area");
			task->publish_status(L"Review status");
			reporter.check(host.chrome->status.text == L"Review status",
				L"TaskView status reaches MainWindow's command surface");
			host.information_progress();
			reporter.check(host.chrome->status.text == L"Review status",
				L"background Items metadata progress cannot replace task status");
			auto toolbar = host.chrome->taskToolbar;
			reporter.check(toolbar.size() >= 2, L"the host installs the task toolbar");
			if (toolbar.size() < 2) return;
			const auto maximize = toolbar[toolbar.size() - 2].command;
			maximize->invoke();
			reporter.check(host.root->maximized && maximize->toolbarText() == L"Restore",
				L"the task Maximize command changes its real host and its dynamic caption");
			maximize->invoke();
			reporter.check(!host.root->maximized && maximize->toolbarText() == L"Maximize",
				L"Restore returns the host to its normal placement");
			host.resize({0, 0, 780, 520});
			reporter.check(rect_equals(taskFrame->bounds, host.center()->client_rect()),
				L"host resize resizes the active task");

			task->analyze();
			reporter.check(task->busy() && !background.empty(), L"analysis runs through the owned task dispatcher");
			const auto replacement = std::make_shared<HostedTestTask>(dispatcher);
			reporter.check(!host.set_task(replacement) && host.task() == task && !replacement->frame(),
				L"a close veto retains the active task without creating its replacement");
			host.fullscreen();
			reporter.check(!host.fullscreen_state() && host.task() == task && host.root->fullscreenChanges == 0,
				L"the mode-bar fullscreen command honors a busy task's close veto");
			drain(background);
			if (!uiQueue.empty())
			{
				auto progress = std::move(uiQueue.front());
				uiQueue.erase(uiQueue.begin());
				progress();
				reporter.check(host.chrome->status.progressPercent == 25,
					L"task progress reaches the host as a percentage");
			}
			drain(uiQueue);
			reporter.check(!task->busy(), L"the UI completion releases analysis ownership");
			host.fullscreen();
			reporter.check(!host.task() && host.fullscreen_state() && host.root->fullscreen &&
				host.canvas()->visible && !host.tree->visible && !host.modes()->visible &&
				host.chrome->taskToolbar.empty() && host.chrome->fullscreenLayout,
				L"entering fullscreen closes the idle task, restores Items and hides browsing chrome");
			reporter.check(taskFrame->closes == 1 && !taskFrame->reactor && !task->frame(),
				L"leaving a task destroys its native child and detaches its reactor");
			host.set_fullscreen(true);
			reporter.check(host.root->fullscreenChanges == 1, L"reentering fullscreen is idempotent");
			host.fullscreen();
			reporter.check(!host.fullscreen_state() && !host.root->fullscreen && host.tree->visible &&
				host.splitter()->visible && host.modes()->visible && !host.chrome->fullscreenLayout,
				L"leaving fullscreen restores the saved browsing panes");

			host.center()->refuseChild = true;
			reporter.check(!host.set_task(replacement) && !host.task() && host.canvas()->visible &&
				host.chrome->taskToolbar.empty(), L"failed task creation leaves Items available with its toolbar");
			host.center()->refuseChild = false;
			reporter.check(host.set_task(replacement), L"task creation can be retried after a platform failure");
			const auto close = host.chrome->taskToolbar.back().command;
			close->invoke();
			reporter.check(!host.task() && host.canvas()->visible && host.root->closes == 0,
				L"the task toolbar Close returns to Items without exiting the application");

			auto cancelling = std::make_shared<HostedTestTask>(dispatcher);
			cancelling->confirmClose = true;
			reporter.check(host.set_task(cancelling), L"the host can reopen a task after Close");
			cancelling->analyze();
			host.exit();
			reporter.check(host.task() == cancelling && cancelling->closing() && host.root->closes == 0,
				L"application exit waits for an accepted cancellation rather than destroying a busy task");
			drain(background);
			drain(uiQueue);
			reporter.check(!host.task() && host.root->closes == 1 && !cancelling->frame(),
				L"the task's deferred closed callback completes pending application exit exactly once");
		}

		void test_sync_workflow(Reporter& reporter)
		{
			reporter.section(L"Synchronize workflow");
			const auto root = platform::unique_temp_folder(L"iw30-sync-workflow-tests");
			std::error_code error;
			const bool owned = std::filesystem::create_directories(root, error);
			reporter.check(owned && !error, L"Sync tests create their own isolated folder");
			if (!owned || error) return;
			struct Cleanup
			{
				std::filesystem::path root;
				~Cleanup()
				{
					std::error_code ignored;
					std::filesystem::remove_all(root, ignored);
				}
			} cleanup{root};
			const auto write = [](const std::filesystem::path& path, const std::string& content)
			{
				std::filesystem::create_directories(path.parent_path());
				std::ofstream stream(path, std::ios::binary | std::ios::trunc);
				stream << content;
			};
			const auto read = [](const std::filesystem::path& path)
			{
				std::ifstream stream(path, std::ios::binary);
				return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
			};
			const auto pair = [&root](const wchar_t* name)
			{
				sync::Options options;
				options.wholeCollection = false;
				options.local = root / name / L"local";
				options.remote = root / name / L"remote";
				std::filesystem::create_directories(options.local);
				std::filesystem::create_directories(options.remote);
				return options;
			};
			const auto countAction = [](const tasks::TaskPlan& plan, const int action)
			{
				size_t count = 0;
				for (const auto& row : plan.rows) if (row.tag == action) ++count;
				return count;
			};

			auto scope = pair(L"scope");
			write(scope.local / L"top.txt", "top");
			write(scope.local / L"nested" / L"child.txt", "nested");
			auto reviewed = sync::analyze(scope);
			reporter.check(reviewed.plan.can_run() && reviewed.plan.rows.size() == 2 &&
				countAction(reviewed.plan, sync::copyToRemote) == 2,
				L"one-folder scope compares the entire selected tree, including nested files");
			if (reviewed.plan.rows.empty())
			{
				emit(reviewed.plan.blockReason + L"\n");
				return;
			}
			reporter.check(reviewed.plan.rows.front().destination.parent_path() == scope.remote / L"nested" ||
				reviewed.plan.rows.back().destination.parent_path() == scope.remote / L"nested",
				L"one-folder scope retains relative destination folders");
			scope.wholeCollection = true;
			scope.collection = {scope.local, root / L"collection" / L"second"};
			write(scope.collection[1] / L"second.txt", "second");
			write(scope.remote / L"unmapped" / L"keep.txt", "unmapped");
			reviewed = sync::analyze(scope);
			reporter.check(reviewed.plan.can_run() && countAction(reviewed.plan, sync::copyToRemote) == 3 &&
				reviewed.plan.count(tasks::RowState::skipped) == 1,
				L"whole collection includes all configured roots without deleting unmapped remote files");
			bool secondMapped = false, localMapped = false;
			for (const auto& row : reviewed.plan.rows)
			{
				secondMapped |= row.destination == scope.remote / L"second" / L"second.txt";
				localMapped |= row.destination == scope.remote / L"local" / L"top.txt";
			}
			reporter.check(secondMapped && localMapped, L"each collection root has a distinct visible remote folder mapping");
			auto unavailableCollection = scope;
			unavailableCollection.collection.push_back(root / L"offline-collection-folder");
			reporter.check(!sync::analyze(unavailableCollection).plan.can_run(),
				L"an unavailable configured Sync folder blocks whole-collection analysis rather than silently omitting it");
			auto oneRoot = scope;
			oneRoot.collection.resize(1);
			reporter.check(sync::analyze(oneRoot).plan.rows.size() == 3,
				L"one remaining collection root keeps the same mapping rather than changing its remote layout");

			auto view = std::make_shared<TaskSync>(L"");
			view->set_options(scope);
			reporter.check(!view->busy() && view->plan().rows.empty() && !view->plan().can_run(),
				L"changing Sync options never starts an implicit analysis");
			view->set_local_folder(root / L"different-current-folder");
			reporter.check(view->options().local == scope.local,
				L"opening Sync from another Items folder preserves the chosen local folder");
			view->set_collection_folders({scope.local});
			reporter.check(!view->busy() && view->plan().rows.empty() &&
				view->options().collection.size() == 1,
				L"changing collection roots invalidates review without analyzing");
			view->add_collection_folder(scope.collection[1]);
			reporter.check(view->options().collection.size() == 2 &&
				view->options().collection.back() == scope.collection[1] && !view->busy() &&
				view->plan().rows.empty(),
				L"adding an explicit collection folder clears Review but never starts analysis");
			view->remove_collection_folder(0);
			view->remove_collection_folder(99);
			reporter.check(view->options().collection == std::vector<std::filesystem::path>{scope.collection[1]} &&
				std::filesystem::exists(scope.local / L"top.txt") && !view->busy(),
				L"removing a collection entry changes only configuration, not files or other roots");
			view->start_run();
			view->start_run();
			reporter.check(!view->busy() && !view->plan().can_run(),
				L"repeated Run without a reviewed analysis cannot start work");

			const auto settingsSection = L"SyncTest-" + root.filename().wstring();
			scope.toRemote = false;
			scope.toLocal = true;
			scope.deleteLocal = true;
			scope.deleteRemote = true;
			sync::save_options(scope, settingsSection);
			const auto retained = sync::load_options(settingsSection);
			reporter.check(retained.wholeCollection && retained.local == scope.local &&
				retained.remote == scope.remote && !retained.toRemote && retained.toLocal &&
				retained.deleteLocal && retained.deleteRemote && retained.collection == scope.collection,
				L"scope, explicit collection folders, folder choices and action options survive settings round-trip");
			auto restoredView = std::make_shared<TaskSync>(settingsSection);
			reporter.check(restoredView->options().collection == scope.collection &&
				!restoredView->busy() && restoredView->plan().rows.empty(),
				L"opening Sync restores its saved collection independently of the current folder and pins");
			restoredView->remove_collection_folder(0);
			reporter.check(sync::load_options(settingsSection).collection ==
				std::vector<std::filesystem::path>{scope.collection[1]} &&
				platform::read_text_setting(settingsSection, L"Collection1").empty(),
				L"removing a collection folder persists the shorter list and clears the stale settings entry");
			restoredView->add_collection_folder(root / L"offline-collection-folder");
			reporter.check(sync::load_options(settingsSection).collection.back() ==
				root / L"offline-collection-folder",
				L"unavailable saved roots remain in settings instead of silently disappearing from the collection");
			auto onlyOne = restoredView->options();
			onlyOne.wholeCollection = false;
			restoredView->set_options(onlyOne);
			reporter.check(!sync::load_options(settingsSection).wholeCollection &&
				sync::load_options(settingsSection).collection == onlyOne.collection,
				L"switching to one-folder scope retains the saved collection list for the next visit");
			platform::clear_settings_section(settingsSection);

			const std::array damagedIntegers{
				platform::IntegerSetting{L"WholeCollection", 1},
				platform::IntegerSetting{L"CollectionCount", 3}};
			platform::write_integer_settings(settingsSection, damagedIntegers);
			const std::array damagedTexts{
				platform::TextSetting{L"Collection0", scope.local.wstring()},
				platform::TextSetting{L"Collection2", scope.collection[1].wstring()}};
			platform::write_text_settings(settingsSection, damagedTexts);
			const auto damagedSettings = sync::load_options(settingsSection);
			reporter.check(damagedSettings.collection.size() == 3 &&
				damagedSettings.collection[0] == scope.local &&
				damagedSettings.collection[1].empty() &&
				damagedSettings.collection[2] == scope.collection[1],
				L"a missing collection setting stays invalid without silently truncating later saved roots");
			platform::clear_settings_section(settingsSection);

			auto invalid = pair(L"invalid");
			auto checkInvalid = [&](const sync::Options& options, const wchar_t* message)
			{
				const auto result = sync::analyze(options);
				reporter.check(!result.plan.can_run() && !result.plan.blockReason.empty(), message);
			};
			auto changed = invalid;
			changed.local.clear();
			checkInvalid(changed, L"an empty local folder blocks analysis with a specific reason");
			changed = invalid;
			changed.remote.clear();
			checkInvalid(changed, L"an empty remote folder blocks analysis");
			changed = invalid;
			changed.local = root / L"does-not-exist";
			checkInvalid(changed, L"an unreadable or missing local tree is not interpreted as empty");
			changed = invalid;
			changed.remote = root / L"does-not-exist";
			checkInvalid(changed, L"an unreadable or missing remote tree is not interpreted as empty");
			changed = invalid;
			changed.remote = invalid.local;
			checkInvalid(changed, L"identical trees are rejected");
			changed.remote = invalid.local / L"nested";
			checkInvalid(changed, L"a nested remote tree is rejected before scanning");
			changed = invalid;
			changed.local = invalid.remote / L"nested";
			checkInvalid(changed, L"a nested local tree is rejected before scanning");
			changed = invalid;
			changed.remote = invalid.local / L"unused" / L"..";
			checkInvalid(changed, L"lexically aliased overlapping roots are rejected");
			changed = invalid;
			changed.wholeCollection = true;
			changed.collection = {invalid.local, root / L"another" / L"local"};
			std::filesystem::create_directories(changed.collection.back());
			checkInvalid(changed, L"two collection roots with the same mapped remote name are ambiguous");
			changed.collection = {invalid.local, invalid.local / L"nested"};
			checkInvalid(changed, L"overlapping collection roots are ambiguous");
			changed = invalid;
			changed.toRemote = false;
			checkInvalid(changed, L"disabling every action blocks analysis");
			write(invalid.local / L"conflict", "file");
			std::filesystem::create_directories(invalid.remote / L"conflict");
			checkInvalid(invalid, L"a file mapped to a folder blocks destructive synchronization");

			auto reparse = pair(L"reparse");
			const auto external = root / L"external";
			write(external / L"untouched.txt", "outside");
			const auto link = reparse.local / L"linked";
			std::filesystem::create_directory_symlink(external, link, error);
			if (!error)
			{
				checkInvalid(reparse, L"nested reparse points cannot be skipped and misread as an empty tree");
				auto rootLink = reparse;
				rootLink.local = link;
				checkInvalid(rootLink, L"a reparse root is refused");
				rootLink.local = link / L"nested";
				std::filesystem::create_directories(external / L"nested");
				checkInvalid(rootLink, L"a reparse ancestor is refused even when the selected leaf is ordinary");
				std::filesystem::remove(link, error);
				reporter.check(read(external / L"untouched.txt") == "outside",
					L"reparse analysis leaves files outside the compared trees untouched");
			}
			else emit(L"  NOTE: directory symlink tests require Windows symlink privilege.\n");

			auto direction = pair(L"direction");
			const auto time = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
			write(direction.local / L"equal.txt", "local");
			write(direction.remote / L"equal.txt", "remote-longer");
			std::filesystem::last_write_time(direction.local / L"equal.txt", time);
			std::filesystem::last_write_time(direction.remote / L"equal.txt", time);
			direction.toLocal = true;
			reviewed = sync::analyze(direction);
			reporter.check(!reviewed.plan.can_run() && reviewed.plan.rows.front().tag == sync::ignore,
				L"equal times and different sizes with both directions enabled are ignored");
			direction.toRemote = false;
			reviewed = sync::analyze(direction);
			reporter.check(reviewed.plan.can_run() && reviewed.plan.rows.front().tag == sync::copyToLocal &&
				reviewed.plan.rows.front().replace,
				L"equal times and different sizes use the sole local direction as an explicit Replace");
			direction.toLocal = false;
			direction.toRemote = true;
			reviewed = sync::analyze(direction);
			reporter.check(reviewed.plan.can_run() && reviewed.plan.rows.front().tag == sync::copyToRemote &&
				reviewed.plan.replacements == 1,
				L"equal times and different sizes use the sole remote direction as an explicit Replace");
			direction.toRemote = false;
			direction.deleteLocal = true;
			reviewed = sync::analyze(direction);
			reporter.check(reviewed.plan.rows.front().tag == sync::ignore,
				L"equal times with neither copy direction enabled are ignored, not deleted");
			direction.toRemote = true;
			direction.toLocal = true;
			write(direction.remote / L"equal.txt", "other");
			std::filesystem::last_write_time(direction.remote / L"equal.txt", time);
			reporter.check(sync::analyze(direction).plan.rows.front().tag == sync::ignore,
				L"equal size and equal modified time are ignored");
			std::filesystem::last_write_time(direction.local / L"equal.txt", time + std::chrono::seconds(1));
			reporter.check(sync::analyze(direction).plan.rows.front().tag == sync::copyToRemote,
				L"a newer local file is not incorrectly treated as equal by a two-second tolerance");
			std::filesystem::last_write_time(direction.remote / L"equal.txt", time + std::chrono::seconds(3));
			reporter.check(sync::analyze(direction).plan.rows.front().tag == sync::copyToLocal,
				L"a newer remote file copies to local when enabled");
			direction.toLocal = false;
			reporter.check(sync::analyze(direction).plan.rows.front().tag == sync::ignore,
				L"a disabled newer-source direction never copies the older side over it");

			auto stale = pair(L"stale");
			write(stale.local / L"a-source.txt", "reviewed source");
			write(stale.local / L"b-target.txt", "reviewed target source");
			write(stale.local / L"c-independent.txt", "independent");
			auto snapshot = std::make_shared<sync::Review>(sync::analyze(stale));
			write(stale.local / L"a-source.txt", "changed after analysis");
			write(stale.remote / L"b-target.txt", "unreviewed destination");
			auto result = snapshot->plan;
			auto run = sync::run_options(snapshot);
			tasks::run_plan(result, {}, run);
			reporter.check(result.count(tasks::RowState::failed) == 2 &&
				result.count(tasks::RowState::success) == 1 &&
				!std::filesystem::exists(stale.remote / L"a-source.txt") &&
				read(stale.remote / L"b-target.txt") == "unreviewed destination" &&
				read(stale.remote / L"c-independent.txt") == "independent",
				L"source and destination drift refuse their rows while independent copies continue");
			reporter.check(tasks::summarize_results(result).find(L"Analyze again") != std::wstring::npos,
				L"changed-row results explicitly request another analysis");
			const auto completedSummary = tasks::summarize_results(result);
			tasks::run_plan(result, {}, run);
			reporter.check(tasks::summarize_results(result) == completedSummary,
				L"a completed snapshot cannot run its rows a second time");

			auto replacement = pair(L"replacement");
			write(replacement.local / L"replace.txt", "new local");
			write(replacement.remote / L"replace.txt", "old remote");
			std::filesystem::last_write_time(replacement.local / L"replace.txt", time + std::chrono::seconds(5));
			std::filesystem::last_write_time(replacement.remote / L"replace.txt", time);
			snapshot = std::make_shared<sync::Review>(sync::analyze(replacement));
			write(replacement.remote / L"replace.txt", "remote changed after review");
			result = snapshot->plan;
			tasks::run_plan(result, {}, sync::run_options(snapshot));
			reporter.check(result.count(tasks::RowState::failed) == 1 &&
				read(replacement.remote / L"replace.txt") == "remote changed after review",
				L"Replace authority does not include a destination changed after Review");
			auto identity = pair(L"identity");
			write(identity.local / L"a-source.txt", "old");
			write(identity.local / L"b-target.txt", "source");
			write(identity.remote / L"b-target.txt", "target");
			write(identity.local / L"c-independent.txt", "independent");
			std::filesystem::last_write_time(identity.local / L"a-source.txt", time);
			std::filesystem::last_write_time(identity.local / L"b-target.txt", time + std::chrono::seconds(5));
			std::filesystem::last_write_time(identity.remote / L"b-target.txt", time);
			snapshot = std::make_shared<sync::Review>(sync::analyze(identity));
			std::filesystem::rename(identity.local / L"a-source.txt", root / L"parked-source.txt");
			write(identity.local / L"a-source.txt", "new");
			std::filesystem::last_write_time(identity.local / L"a-source.txt", time);
			std::filesystem::rename(identity.remote / L"b-target.txt", root / L"parked-target.txt");
			write(identity.remote / L"b-target.txt", "change");
			std::filesystem::last_write_time(identity.remote / L"b-target.txt", time);
			result = snapshot->plan;
			tasks::run_plan(result, {}, sync::run_options(snapshot));
			reporter.check(result.count(tasks::RowState::failed) == 2 &&
				result.count(tasks::RowState::success) == 1 &&
				!std::filesystem::exists(identity.remote / L"a-source.txt") &&
				read(identity.remote / L"b-target.txt") == "change" &&
				read(identity.remote / L"c-independent.txt") == "independent",
				L"file identity changes refuse same-size same-time source and destination replacements while independent rows continue");

			auto parentIdentity = pair(L"parent-identity");
			write(parentIdentity.local / L"nested" / L"source.txt", "source");
			snapshot = std::make_shared<sync::Review>(sync::analyze(parentIdentity));
			std::filesystem::rename(parentIdentity.remote, parentIdentity.remote.parent_path() / L"remote-reviewed");
			std::filesystem::create_directories(parentIdentity.remote);
			result = snapshot->plan;
			tasks::run_plan(result, {}, sync::run_options(snapshot));
			reporter.check(result.count(tasks::RowState::failed) == 1 &&
				!std::filesystem::exists(parentIdentity.remote / L"nested" / L"source.txt"),
				L"a replacement destination parent cannot inherit authority to receive a reviewed nested copy");

			auto readOnlyReplacement = pair(L"read-only-replacement");
			write(readOnlyReplacement.local / L"replace.txt", "new");
			write(readOnlyReplacement.remote / L"replace.txt", "old");
			std::filesystem::last_write_time(
				readOnlyReplacement.local / L"replace.txt", time + std::chrono::seconds(5));
			std::filesystem::last_write_time(readOnlyReplacement.remote / L"replace.txt", time);
			std::filesystem::permissions(readOnlyReplacement.remote / L"replace.txt",
				std::filesystem::perms::owner_read, std::filesystem::perm_options::replace, error);
			if (!error)
			{
				const auto readOnlyReview = sync::analyze(readOnlyReplacement);
				reporter.check(!readOnlyReview.plan.can_run() &&
					readOnlyReview.plan.rows.front().state == tasks::RowState::blocked &&
					readOnlyReview.plan.rows.front().detail.find(L"read-only") != std::wstring::npos,
					L"a read-only replacement target blocks synchronization during Review");
				std::filesystem::permissions(readOnlyReplacement.remote / L"replace.txt",
					std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, error);
			}
			else emit(L"  NOTE: read-only Sync test could not set file permissions.\n");

			auto deletion = pair(L"deletion");
			deletion.toRemote = false;
			deletion.deleteLocal = true;
			deletion.deleteRemote = true;
			write(deletion.local / L"a-local.txt", "local");
			write(deletion.remote / L"b-remote.txt", "remote");
			snapshot = std::make_shared<sync::Review>(sync::analyze(deletion));
			const auto confirmation = sync::deletion_confirmation(snapshot->plan);
			reporter.check(confirmation.warning && confirmation.defaultButton == 2 &&
				confirmation.heading.find(L"2 files") != std::wstring::npos &&
				confirmation.message.find(L"1 local file and 1 remote file") != std::wstring::npos &&
				confirmation.message.find(L"cannot be recovered from the Recycle Bin") != std::wstring::npos,
				L"permanent-delete confirmation states the total, counts each side, and defaults to Cancel");
			const bool noConfirmation = sync::confirm_deletions(snapshot->plan, {});
			const bool declined = sync::confirm_deletions(snapshot->plan,
				[](const platform::ChoiceDefinition&) { return 2; });
			reporter.check(!noConfirmation && !declined &&
				snapshot->plan.count(tasks::RowState::ready) == 2 &&
				std::filesystem::exists(deletion.local / L"a-local.txt") &&
				std::filesystem::exists(deletion.remote / L"b-remote.txt"),
				L"deletion cannot proceed without affirmative confirmation and Cancel leaves Review unchanged");
			reporter.check(sync::confirm_deletions(snapshot->plan,
				[](const platform::ChoiceDefinition&) { return 1; }),
				L"the explicit Synchronize answer authorizes the counted deletion plan");
			write(deletion.remote / L"a-local.txt", "appeared after review");
			result = snapshot->plan;
			tasks::run_plan(result, {}, sync::run_options(snapshot));
			reporter.check(result.count(tasks::RowState::failed) == 1 &&
				result.count(tasks::RowState::success) == 1 &&
				std::filesystem::exists(deletion.local / L"a-local.txt") &&
				!std::filesystem::exists(deletion.remote / L"b-remote.txt"),
				L"an appeared counterpart refuses a stale delete while an independent reviewed deletion completes");
			reporter.check(sync::affected_folders(result).empty(),
				L"remote-only successes do not claim that a local folder changed");
			auto vanishedRoot = pair(L"vanished-root");
			vanishedRoot.toRemote = false;
			vanishedRoot.deleteLocal = true;
			write(vanishedRoot.local / L"keep.txt", "local only");
			snapshot = std::make_shared<sync::Review>(sync::analyze(vanishedRoot));
			std::filesystem::rename(vanishedRoot.remote, vanishedRoot.remote.parent_path() / L"remote-moved");
			result = snapshot->plan;
			tasks::run_plan(result, {}, sync::run_options(snapshot));
			reporter.check(result.count(tasks::RowState::failed) == 1 &&
				std::filesystem::exists(vanishedRoot.local / L"keep.txt"),
				L"a remote root that disappears after analysis never authorizes local deletion");
			std::filesystem::create_directories(vanishedRoot.remote);
			result = snapshot->plan;
			tasks::run_plan(result, {}, sync::run_options(snapshot));
			reporter.check(result.count(tasks::RowState::failed) == 1 &&
				std::filesystem::exists(vanishedRoot.local / L"keep.txt"),
				L"a replacement remote root with the same absent counterparts does not inherit deletion authority");

			auto localDelete = pair(L"local-delete-cancel");
			localDelete.toRemote = false;
			localDelete.deleteLocal = true;
			write(localDelete.local / L"a.txt", "delete first");
			write(localDelete.local / L"b.txt", "retain second");
			snapshot = std::make_shared<sync::Review>(sync::analyze(localDelete));
			result = snapshot->plan;
			std::stop_source deleteStop;
			run = sync::run_options(snapshot);
			run.progress = [&deleteStop](const size_t complete, size_t)
			{
				if (complete == 1) deleteStop.request_stop();
			};
			tasks::run_plan(result, deleteStop.get_token(), run);
			reporter.check(result.count(tasks::RowState::success) == 1 &&
				result.count(tasks::RowState::notRun) == 1 &&
				!std::filesystem::exists(localDelete.local / L"a.txt") &&
				std::filesystem::exists(localDelete.local / L"b.txt") &&
				sync::affected_folders(result) == std::vector<std::filesystem::path>{localDelete.local},
				L"cancelling permanent deletion does not roll it back and still refreshes the changed local folder");

			auto cancellation = pair(L"cancellation");
			cancellation.toRemote = false;
			cancellation.toLocal = true;
			write(cancellation.remote / L"a.txt", "first");
			write(cancellation.remote / L"b.txt", "second");
			snapshot = std::make_shared<sync::Review>(sync::analyze(cancellation));
			result = snapshot->plan;
			std::stop_source stop;
			run = sync::run_options(snapshot);
			run.progress = [&stop](const size_t complete, const size_t) { if (complete == 1) stop.request_stop(); };
			tasks::run_plan(result, stop.get_token(), run);
			reporter.check(result.count(tasks::RowState::success) == 1 &&
				result.count(tasks::RowState::notRun) == 1 &&
				read(cancellation.local / L"a.txt") == "first" &&
				!std::filesystem::exists(cancellation.local / L"b.txt"),
				L"cancellation preserves completed local copies and marks unreached rows Not run");
			const auto affected = sync::affected_folders(result);
			reporter.check(affected.size() == 1 && affected.front() == cancellation.local,
				L"partial completion identifies the affected local folder for refresh");
			reporter.check(tasks::summarize_results(result).find(L"1 not run") != std::wstring::npos,
				L"cancellation results cannot be mistaken for full success");
			stop = std::stop_source{};
			const auto cancelledAnalysis = sync::analyze(cancellation, stop.get_token(),
				[&stop](size_t, size_t) { stop.request_stop(); });
			reporter.check(!cancelledAnalysis.plan.can_run() && cancelledAnalysis.plan.rows.empty() &&
				cancelledAnalysis.plan.blockReason.find(L"cancelled") != std::wstring::npos,
				L"cancelled scans discard the partial review and report cancellation");
			const auto preCancelled = sync::analyze(cancellation, stop.get_token());
			reporter.check(!preCancelled.plan.can_run() && preCancelled.plan.rows.empty(),
				L"analysis honors cancellation before scanning any entries");
		}

		void test_startup_dispatch(Reporter& reporter)
		{
			reporter.section(L"startup UI dispatch");
			std::thread uiThread([&reporter]
			{
			const platform::Runtime runtime(platform::RuntimeMode::userInterface);
			reporter.check(static_cast<bool>(runtime), L"the UI runtime initializes its dispatcher");
			if (!runtime) return;
			const auto delivered = std::make_shared<std::atomic_bool>(false);
			reporter.check(platform::queue_ui([delivered] { delivered->store(true); }),
				L"startup workers can queue UI completion before the outer message loop starts");
			reporter.check(platform::run_with_status({}, L"Test", L"Waiting for startup completion", [delivered]
			{
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
				while (!delivered->load() && std::chrono::steady_clock::now() < deadline)
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				return delivered->load();
			}), L"startup completion is delivered instead of silently losing the initial folder scan");
			platform::WindowOptions options;
			options.className = "ImageWalker30.NativeFullscreenTest";
			options.title = L"Native fullscreen regression";
			options.bounds = {80, 90, 400, 300};
			options.visible = false;
			const auto frame = platform::create_top_level_frame(std::make_shared<platform::FrameReactor>(), options);
			reporter.check(frame && frame->native_handle() != 0, L"the real HWND backend creates a hidden top-level window");
			if (frame)
			{
				const auto placement = frame->placement();
				const auto client = frame->client_rect();
				frame->set_fullscreen(true);
				const auto full = frame->client_rect();
				frame->set_fullscreen(true);
				reporter.check(rect_equals(frame->placement().normalBounds, placement.normalBounds) &&
					full.width >= client.width && full.height >= client.height,
					L"native fullscreen keeps the normal placement and occupies the monitor");
				frame->set_fullscreen(false);
				reporter.check(rect_equals(frame->placement().normalBounds, placement.normalBounds) &&
					rect_equals(frame->client_rect(), client) && frame->placement().maximized == placement.maximized,
					L"leaving native fullscreen restores the original HWND placement and client extent");
				frame->close();
			}
			});
			uiThread.join();
		}

		void test_color(Reporter& reporter)
		{
			reporter.section(L"color");
			constexpr color value{0x12, 0x34, 0x56, 0x78};
			reporter.check(value.a == 0x78,
			               L"color exposes RGBA channels");
			reporter.check(value.pack() == 0x00563412, L"color packs to COLORREF byte order");
			reporter.check(color::from_packed(value.pack(), value.a) == value, L"packed color round-trips");
			reporter.check(colors::black.pack() == 0x00000000, L"black palette color is stable");
			reporter.check(colors::dark_gray.pack() == 0x00373737, L"dark gray palette color is stable");
			reporter.check(colors::light_gray.pack() == 0x00eeeeee, L"light gray palette color is stable");
			reporter.check(colors::light_gray.with_alpha(42) == color{238, 238, 238, 42},
			               L"with_alpha preserves RGB channels");
			reporter.check(platform::calc_hande_color(false, false, false) ==
			               platform::system_color(platform::SystemColor::face),
			               L"an idle handle uses the system face color");
			reporter.check(platform::calc_hande_color(true, true, true) ==
			               platform::system_color(platform::SystemColor::highlight),
			               L"a selected handle takes precedence over hover and checked state");
		}

		void test_dpi_metrics(Reporter& reporter)
		{
			reporter.section(L"ui DPI metrics");
			reporter.check(ui::scale_metric(24, 96) == 24, L"96 DPI preserves logical metrics");
			reporter.check(ui::scale_metric(24, 120) == 30, L"125% DPI scales logical metrics");
			reporter.check(ui::scale_metric(24, 144) == 36, L"150% DPI scales logical metrics");
			reporter.check(ui::scale_metric(24, 192) == 48, L"200% DPI scales logical metrics");
			reporter.check(ui::unscale_metric(48, 192) == 24, L"physical metrics round-trip at 200% DPI");
			reporter.check(ui::scale_metric(7, 0) == 7, L"a missing DPI falls back to 96 DPI");
		}

		void test_supported_images(Reporter& reporter)
		{
			reporter.section(L"files::is_supported_image");
			reporter.check(files::is_supported_image(L"photo.jpg"), L"lowercase jpg is supported");
			reporter.check(files::is_supported_image(L"PHOTO.JPG"), L"uppercase JPG is supported (case-insensitive)");
			reporter.check(files::is_supported_image(L"a.jpeg"), L"jpeg is supported");
			reporter.check(files::is_supported_image(L"a.PnG"), L"mixed-case png is supported");
			reporter.check(files::is_supported_image(L"scan.tiff"), L"tiff is supported");
			reporter.check(files::is_supported_image(L"icon.ico"), L"ico is supported");
			reporter.check(!files::is_supported_image(L"notes.txt"), L"txt is not supported");
			reporter.check(!files::is_supported_image(L"archive.zip"), L"zip is not supported");
			reporter.check(!files::is_supported_image(L"README"), L"extension-less files are not supported");
			reporter.check(!files::is_supported_image(L"trick.jpgx"), L"a superset extension is not supported");
			std::error_code error;
			const auto root = platform::unique_temp_folder(L"iw30-eligibility-tests");
			std::filesystem::create_directories(root / L"folder.jpg", error);
			const auto photo = root / L"photo.jpg";
			std::ofstream(photo) << "photo";
			reporter.check(files::is_editable_image(photo), L"a writable image file is eligible for Edit");
			reporter.check(!files::is_editable_image(root / L"folder.jpg") &&
				!files::is_editable_image(root / L"missing.jpg"),
				L"image-named folders and missing files are not editable photos");
			std::filesystem::permissions(photo, std::filesystem::perms::owner_read,
				std::filesystem::perm_options::replace, error);
			reporter.check(!error && !files::is_editable_image(photo), L"a read-only photo is not editable");
			std::filesystem::permissions(photo, std::filesystem::perms::owner_all,
				std::filesystem::perm_options::replace, error);
			std::filesystem::remove_all(root, error);
		}

		void test_selection_model(Reporter& reporter)
		{
			reporter.section(L"ui::SelectionModel");
			ui::SelectionModel selection;

			selection.reset(5);
			reporter.check(selection.size() == 0, L"reset clears the selection");
			reporter.check(selection.focus() == 0, L"reset focuses the first item when non-empty");

			selection.click(2, false, false);
			reporter.check(selection.size() == 1 && selection.contains(2) && selection.focus() == 2,
			               L"plain click selects a single item and focuses it");

			selection.click(4, true, false);
			reporter.check(selection.size() == 2 && selection.contains(4), L"ctrl+click adds to the selection");

			selection.click(2, true, false);
			reporter.check(selection.size() == 1 && !selection.contains(2) && selection.contains(4),
			               L"ctrl+click on a selected item toggles it off");

			selection.reset(10);
			selection.click(2, false, false);
			selection.click(5, false, true);
			reporter.check(selection.size() == 4 && selection.contains(2) && selection.contains(5) &&
			               !selection.contains(1) && !selection.contains(6),
			               L"shift+click selects an inclusive range from the anchor");
			reporter.check(selection.focus() == 5, L"shift+click moves focus to the clicked item");

			selection.click(8, true, false);
			selection.click(6, true, true);
			reporter.check(selection.size() == 7 && selection.contains(6) && selection.contains(7) &&
			               selection.contains(8) && selection.contains(2),
			               L"ctrl+shift+click adds a range without discarding the existing selection");

			selection.reset(4);
			selection.select_all();
			reporter.check(selection.size() == 4, L"select_all selects every item");
			selection.clear();
			reporter.check(selection.size() == 0, L"clear empties the selection");

			selection.reset(4);
			reporter.check(selection.move_focus(1) == 1 && selection.focus() == 1 && selection.size() == 1,
			               L"move_focus advances focus and collapses to a single selection");
			reporter.check(selection.move_focus(-5) == 0, L"move_focus clamps at the first item");
			reporter.check(selection.move_focus(10) == 3, L"move_focus clamps at the last item");

			selection.reset(10);
			selection.click(3, false, false);
			selection.navigate(6, false, true);
			reporter.check(selection.size() == 4 && selection.contains(3) && selection.contains(6),
			               L"shift navigation extends the range from the anchor");
			selection.navigate(1, false, false);
			reporter.check(selection.size() == 1 && selection.contains(1),
			               L"plain navigation collapses to a single item");

			selection.reset(6);
			selection.compare_select(2, false);
			selection.compare_select(4, true);
			reporter.check(selection.focus() == 2 && selection.secondary() == 4 && selection.size() == 2,
			               L"comparison selection retains one primary and one secondary item");
			selection.compare_select(5, true);
			reporter.check(selection.focus() == 2 && selection.secondary() == 5 && selection.size() == 2,
			               L"selecting another secondary replaces the previous secondary");
			selection.compare_select(1, false);
			reporter.check(selection.focus() == 1 && selection.secondary() == -1 && selection.size() == 1,
			               L"selecting a new primary clears the secondary item");
		}

		void test_marquee(Reporter& reporter)
		{
			reporter.section(L"ui::SelectionModel marquee");
			const std::vector items{
				make_item(0, {0, 0, 50, 50}), make_item(1, {60, 0, 50, 50}),
				make_item(2, {0, 60, 50, 50})
			};

			ui::SelectionModel selection;
			selection.reset(3);
			selection.begin_marquee(false);
			selection.marquee(items, {0, 0, 55, 55});
			reporter.check(selection.size() == 1 && selection.contains(0),
			               L"a marquee selects only intersecting items");
			selection.marquee(items, {0, 0, 200, 200});
			reporter.check(selection.size() == 3, L"an enlarged marquee re-evaluates from scratch");

			selection.reset(3);
			selection.click(2, false, false);
			selection.begin_marquee(true);
			selection.marquee(items, {0, 0, 55, 55});
			reporter.check(selection.size() == 2 && selection.contains(0) && selection.contains(2),
			               L"an additive marquee preserves the prior selection");
		}

		void test_collection_state(Reporter& reporter)
		{
			reporter.section(L"CollectionState");
			CollectionState state;
			state.set_items({
				{L"folder", files::ItemKind::folder}, {L"b.jpg", files::ItemKind::image},
				{L"a.txt", files::ItemKind::document}, {L"a.jpg", files::ItemKind::image}
			});
			reporter.check(state.items().size() == 4 && state.photo_count() == 2,
			               L"collection state retains all items and counts images");
			reporter.check(state.visible_items().size() == 2 && state.visible_items()[0].path == L"a.jpg",
			               L"the photos-only projection follows the selected sort order");
			state.set_photos_only(false);
			reporter.check(
				state.visible_items().size() == 4 && state.visible_items()[0].kind == files::ItemKind::folder,
				L"clearing the filter reveals all items while keeping folders first");
		}

		void test_navigation_history(Reporter& reporter)
		{
			reporter.section(L"NavigationHistory");
			NavigationHistory history;
			reporter.check(!history.can_go_back() && !history.can_go_forward(),
			               L"an empty history offers neither direction");

			history.visit(LR"(C:\a)");
			history.visit(LR"(C:\b)");
			history.visit(LR"(C:\c)");
			reporter.check(history.size() == 3 && history.can_go_back() && !history.can_go_forward(),
			               L"visiting folders builds a back stack with nothing ahead");

			history.visit(LR"(C:\c)");
			reporter.check(history.size() == 3, L"revisiting the current folder does not grow the stack");
			history.visit(LR"(C:\C)");
			reporter.check(history.size() == 3, L"a case-different repeat of the current folder is ignored");

			const auto back = history.go(-1);
			reporter.check(back && paths::equal(*back, LR"(C:\b)") && history.can_go_forward(),
			               L"going back returns the previous folder and opens a forward branch");

			history.visit(LR"(C:\d)");
			reporter.check(history.size() == 3 && !history.can_go_forward(),
			               L"visiting from mid-history discards the forward branch");

			reporter.check(!history.go(1) && history.size() == 3,
			               L"going forward past the end is refused without disturbing the stack");

			history.go(-1);
			history.rollback();
			reporter.check(paths::equal(history.current(), LR"(C:\d)"),
			               L"rollback restores the cursor when the target could not be opened");

			history.forget(LR"(C:\d)");
			reporter.check(history.size() == 2 && !paths::equal(history.current(), LR"(C:\d)"),
			               L"forgetting a folder removes it and keeps the cursor in range");
		}

		void test_parent_folder(Reporter& reporter)
		{
			reporter.section(L"parent_folder");
			reporter.check(!parent_folder({}), L"an empty path has no parent");
			reporter.check(!parent_folder(LR"(C:\)"), L"a drive root has no parent folder");
			const auto parent = parent_folder(LR"(C:\a\b)");
			reporter.check(parent && paths::equal(*parent, LR"(C:\a)"), L"a nested folder reports its parent");
			const auto top = parent_folder(LR"(C:\a)");
			reporter.check(top && paths::equal(*top, LR"(C:\)"), L"a top-level folder reports the drive root");
		}

		void test_geometry(Reporter& reporter)
		{
			reporter.section(L"ui::Rect geometry");
			constexpr recti rect{10, 20, 30, 40};
			reporter.check(rect.bottom() == 60, L"right/bottom derive from origin and extent");
			reporter.check(rect.contains({39, 59}),
			               L"contains includes the top-left and last interior pixel");
			reporter.check(!rect.contains({9, 20}),
			               L"contains excludes the exclusive right/bottom edge");

			constexpr recti a{0, 0, 10, 10};
			constexpr recti overlapping{5, 5, 10, 10};
			constexpr recti separate{20, 20, 5, 5};
			constexpr recti touching{10, 0, 5, 10};
			reporter.check(a.intersects(overlapping), L"overlapping rectangles intersect");
			reporter.check(!a.intersects(separate), L"disjoint rectangles do not intersect");
			reporter.check(!a.intersects(touching), L"edge-adjacent rectangles do not intersect");

			ui::HorizontalScroll horizontal;
			horizontal.layout({10, 20, 200, 40}, 500, 400);
			reporter.check(horizontal.visible() && horizontal.offset == 300 && horizontal.track.y == 48,
			               L"horizontal scrolling clamps its offset and places the track at the bottom");
			horizontal.set_thumb_position(horizontal.track.x);
			reporter.check(horizontal.offset == 0, L"horizontal thumb movement updates the content offset");
		}

		void test_collage_layout(Reporter& reporter)
		{
			reporter.section(L"ui::layout_collage");
			constexpr recti canvas{10, 20, 800, 600};

			reporter.check(ui::layout_collage(canvas, {}).empty(), L"an empty cell list produces no rectangles");
			reporter.check(ui::layout_collage({0, 0, 0, 0}, {{4, 3}, {4, 3}}).empty(),
			               L"an empty canvas produces no rectangles");

			const auto single = ui::layout_collage(canvas, {{4, 3}});
			reporter.check(single.size() == 1 && rect_equals(single.front(), canvas),
			               L"a single cell takes the whole canvas");

			const auto pair = ui::layout_collage(canvas, {{4, 3}, {4, 3}});
			reporter.check(pair.size() == 2 && pair[0].x == canvas.x && pair[0].right() == pair[1].x &&
			               pair[1].right() == canvas.right(),
			               L"a wide canvas splits vertically with no gap or overlap");
			reporter.check(pair[0].width > pair[1].width,
			               L"the cell nearest the canvas aspect takes the larger share");

			// Every cell must be non-empty, inside the canvas, and disjoint from the others.
			std::vector<sizei> dimensions;
			for (size_t count = 2; count <= 30; ++count)
			{
				while (dimensions.size() < count)
					dimensions.push_back(dimensions.size() % 3 == 0 ? sizei{3, 4} : sizei{4, 3});
				const auto cells = ui::layout_collage(canvas, dimensions);
				if (cells.size() != (std::min)(count, size_t{24}))
				{
					reporter.check(false, L"the cell count is capped at twenty-four");
					continue;
				}
				std::int64_t area = 0;
				bool contained = true;
				bool disjoint = true;
				for (size_t index = 0; index < cells.size(); ++index)
				{
					const recti cell = cells[index];
					contained = contained && !cell.is_empty() && cell.x >= canvas.x && cell.y >= canvas.y &&
						cell.right() <= canvas.right() && cell.bottom() <= canvas.bottom();
					area += static_cast<std::int64_t>(cell.width) * cell.height;
					for (size_t other = index + 1; other < cells.size(); ++other)
						disjoint = disjoint && !cell.intersects(cells[other]);
				}
				reporter.check(contained, L"every collage cell is non-empty and inside the canvas");
				reporter.check(disjoint, L"collage cells never overlap");
				reporter.check(area == static_cast<std::int64_t>(canvas.width) * canvas.height,
				               L"collage cells tile the canvas exactly");
			}

			// One pixel per cell is the point where the recursive split runs out of room.
			const auto degenerate = ui::layout_collage({0, 0, 3, 1}, {{1, 1}, {1, 1}, {1, 1}, {1, 1}});
			reporter.check(degenerate.size() == 4 && std::ranges::none_of(degenerate, &recti::is_empty),
			               L"a canvas narrower than the cell count still yields non-empty cells");
		}

		void test_flex_layout(Reporter& reporter)
		{
			reporter.section(L"ui::FlexBox layout");

			ui::Element row;
			row.style.axis = ui::Axis::row;
			row.style.gap = 10;
			ui::Element fixed;
			fixed.id = 0;
			fixed.style.basis = 50;
			ui::Element flexible;
			flexible.id = 1;
			flexible.style.basis = 50;
			flexible.style.grow = 1;
			row.children = {fixed, flexible};
			ui::FlexBox::arrange(row, {0, 0, 200, 100});
			reporter.check(rect_equals(row.children[0].bounds, {0, 0, 50, 100}),
			               L"a non-growing child keeps its basis width");
			reporter.check(row.children[1].bounds.x == 60 && row.children[1].bounds.width == 140,
			               L"a growing child absorbs remaining space after the gap");

			ui::Element column;
			column.style.axis = ui::Axis::column;
			column.style.padding = 5;
			ui::Element first;
			first.intrinsic.height = 30;
			ui::Element second;
			second.intrinsic.height = 30;
			column.children = {first, second};
			ui::FlexBox::arrange(column, {0, 0, 100, 200});
			reporter.check(rect_equals(column.children[0].bounds, {5, 5, 90, 30}),
			               L"column layout honours padding and intrinsic height");
			reporter.check(column.children[1].bounds.y == 35, L"column children stack below one another");

			ui::Element wrap;
			wrap.style.wrap = true;
			wrap.style.gap = 10;
			ui::Element tile;
			tile.style.basis = 60;
			tile.intrinsic.height = 40;
			wrap.children = {tile, tile, tile};
			ui::FlexBox::arrange(wrap, {0, 0, 150, 1000});
			reporter.check(wrap.children[0].bounds.y == 0 && wrap.children[1].bounds.x == 70,
			               L"tiles flow horizontally until the row is full");
			reporter.check(wrap.children[2].bounds.x == 0 && wrap.children[2].bounds.y == 50,
			               L"an overflowing tile wraps to the next line");

			ui::Element list;
			list.style.padding = 4;
			list.style.gap = 2;
			list.children = {ui::Element{}, ui::Element{}, ui::Element{}};
			const sizei listSize = ui::FlexBox::arrange_list(list, {0, 0, 100, 500}, 20);
			reporter.check(listSize.height == 72, L"arrange_list reports padded, gapped content height");
			reporter.check(list.children[0].bounds.y == 4 && list.children[0].bounds.height == 20,
			               L"list rows use the requested row height");

			ui::Element parent;
			parent.id = 9;
			parent.bounds = {0, 0, 100, 100};
			parent.children = {make_item(5, {10, 10, 20, 20})};
			const ui::Element* childHit = ui::FlexBox::hit_test(parent, {15, 15});
			reporter.check(childHit && childHit->id == 5, L"hit_test returns the topmost child under the point");
			const ui::Element* parentHit = ui::FlexBox::hit_test(parent, {50, 50});
			reporter.check(parentHit && parentHit->id == 9, L"hit_test falls back to the container");
			reporter.check(ui::FlexBox::hit_test(parent, {200, 200}) == nullptr,
			               L"hit_test misses points outside the container");
		}

		void test_vertical_scroll(Reporter& reporter)
		{
			reporter.section(L"ui::VerticalScroll");
			ui::VerticalScroll scroll;

			scroll.layout({0, 0, 100, 200}, 200, 0);
			reporter.check(!scroll.visible() && scroll.maximum() == 0,
			               L"content that fits the viewport hides the scrollbar");

			scroll.layout({0, 0, 100, 100}, 300, 0);
			reporter.check(scroll.visible() && scroll.maximum() == 200,
			               L"overflowing content exposes a scrollbar with the correct range");
			reporter.check(scroll.viewport({0, 0, 100, 100}).width == 88,
			               L"a visible scrollbar reserves track width from the viewport");
			reporter.check(scroll.scroll_by(50) && scroll.offset == 50, L"scroll_by advances within range");
			reporter.check(scroll.scroll_by(1000) && scroll.offset == 200, L"scroll_by clamps to the maximum");
			reporter.check(!scroll.scroll_by(10), L"scroll_by reports no change at the maximum");
			reporter.check(scroll.scroll_by(-500) && scroll.offset == 0, L"scroll_by clamps to the minimum");
			reporter.check(!scroll.scroll_by(-10), L"scroll_by reports no change at the minimum");

			scroll.layout({0, 0, 100, 100}, 300, 0);
			scroll.set_thumb_position(1000);
			reporter.check(scroll.offset == 200, L"dragging the thumb past the end pins to the maximum");
			scroll.set_thumb_position(-50);
			reporter.check(scroll.offset == 0, L"dragging the thumb before the start pins to the minimum");

			scroll.layout({0, 0, 20, 10}, 100, 0);
			reporter.check(scroll.thumb.height <= scroll.track.height,
			               L"a vertical thumb remains inside a very short track");
			scroll.layout({0, 0, 200, 400}, 4000, 0, ui::scale_metric(12, 192), ui::scale_metric(28, 192));
			reporter.check(scroll.track.width == 24 && scroll.thumb.height == 56,
			               L"a vertical scrollbar honors scaled thickness and minimum thumb metrics");
			ui::HorizontalScroll horizontal;
			horizontal.layout({0, 0, 10, 20}, 100, 0);
			reporter.check(horizontal.thumb.width <= horizontal.track.width,
			               L"a horizontal thumb remains inside a very narrow track");
			horizontal.layout({0, 0, 400, 200}, 4000, 0, ui::scale_metric(12, 192), ui::scale_metric(28, 192));
			reporter.check(horizontal.track.height == 24 && horizontal.thumb.width == 56,
			               L"a horizontal scrollbar honors scaled thickness and minimum thumb metrics");
		}

		void test_zoom_model(Reporter& reporter)
		{
			reporter.section(L"ui::ZoomModel");
			ui::ZoomModel zoom;
			zoom.set_source({1600, 1200});
			const recti viewport{0, 0, 800, 600};
			reporter.check(std::abs(zoom.fit_scale(viewport) - 0.5) < 1e-9,
			               L"fit scale contains a large image");
			reporter.check(rect_equals(zoom.destination(viewport), viewport),
			               L"a fitted image is centered in its viewport");

			const pointi anchor{200, 150};
			zoom.set_explicit(1.0, viewport, anchor);
			const recti anchored = zoom.destination(viewport);
			reporter.check(anchored.x == -200 && anchored.y == -150,
			               L"explicit zoom keeps the anchored source point fixed");

			zoom.pan_by(viewport, 10000, 10000);
			const recti firstEdge = zoom.destination(viewport);
			reporter.check(firstEdge.x == 0 && firstEdge.y == 0,
			               L"panning clamps at the first image edges");
			zoom.pan_by(viewport, -10000, -10000);
			const recti lastEdge = zoom.destination(viewport);
			reporter.check(lastEdge.right() == viewport.right() && lastEdge.bottom() == viewport.bottom(),
			               L"panning clamps at the last image edges");
			reporter.check(ui::ZoomModel::accelerated_pan_offset(60, 120) == 90 &&
			               ui::ZoomModel::accelerated_pan_offset(-60, 120) == -90,
			               L"drag panning accelerates symmetrically with distance from its origin");
			reporter.check(ui::ZoomModel::accelerated_pan_offset(240, 120) == 720,
			               L"a longer pointer drag covers substantially more image distance");

			zoom.set_fit();
			zoom.step(1, viewport, {400, 300});
			reporter.check(!zoom.is_fit() && std::abs(zoom.effective_scale(viewport) - 0.67) < 1e-9,
			               L"stepping in uses the next fixed ladder stop above Fit");
			zoom.step(-1, viewport, {400, 300});
			reporter.check(zoom.is_fit(), L"stepping out across the fit scale lands on Fit");
			zoom.step(-1, viewport, {400, 300});
			reporter.check(zoom.is_fit(), L"Fit is the floor for stepped zoom");

			ui::ZoomModel smallImage;
			smallImage.set_source({100, 50});
			reporter.check(std::abs(smallImage.fit_scale(viewport) - 1.0) < 1e-9,
			               L"Fit does not enlarge a small image");

			zoom.set_source({1600, 1200});
			zoom.set_explicit(0.75, viewport, {400, 300});
			zoom.carry_to_source({400, 300}, viewport);
			reporter.check(!zoom.is_fit() && zoom.is_effectively_fit(viewport) &&
			               std::abs(zoom.effective_scale(viewport) - 1.0) < 1e-9,
			               L"a carried scale below the next image's Fit is remembered while that image is fitted");
			zoom.carry_to_source({1600, 1200}, viewport);
			reporter.check(std::abs(zoom.effective_scale(viewport) - 0.75) < 1e-9,
			               L"the carried scale resumes on the next image large enough to use it");
			zoom.carry_to_source({400, 300}, viewport);
			zoom.set_explicit(0.5, viewport, {400, 300});
			reporter.check(!zoom.is_effectively_fit(viewport) &&
			               std::abs(zoom.effective_scale(viewport) - 0.5) < 1e-9,
			               L"an explicitly chosen scale below Fit remains reachable");
		}

		void test_loading_model(Reporter& reporter)
		{
			reporter.section(L"ui::LoadingModel");
			ui::LoadingModel loading;
			loading.begin(7, 800, 600, true);
			reporter.check(loading.phase() == ui::LoadingModel::Phase::placeholder && loading.needs_more(),
			               L"a shaped placeholder retains the requested resolution need");
			loading.seed_thumbnail(160, 120);
			reporter.check(loading.phase() == ui::LoadingModel::Phase::thumbnail && loading.provisional(),
			               L"a thumbnail improves the placeholder but remains provisional");
			reporter.check(!loading.apply(6, ui::LoadingModel::Phase::source, 800, 600),
			               L"a stale generation cannot publish pixels");
			reporter.check(!loading.apply(7, ui::LoadingModel::Phase::placeholder, 1600, 1200),
			               L"a lower phase cannot replace a better representation");
			reporter.check(loading.apply(7, ui::LoadingModel::Phase::source, 800, 600) &&
			               !loading.needs_more() && !loading.provisional(),
			               L"an adequate source decode settles the requested need");
			reporter.check(!loading.apply(7, ui::LoadingModel::Phase::source, 400, 300),
			               L"a smaller same-phase result cannot move quality backwards");
			reporter.check(!loading.apply(7, ui::LoadingModel::Phase::source, 1200, 500),
			               L"a wider but shorter same-phase result cannot move quality backwards");

			loading.begin(8, 1600, 1200, false);
			reporter.check(loading.fail(8, ui::LoadingModel::Failure::unreadable) &&
			               loading.failure() == ui::LoadingModel::Failure::unreadable && loading.needs_more(),
			               L"failure is visible without dropping the outstanding need");
			reporter.check(!loading.fail(7, ui::LoadingModel::Failure::unsupported),
			               L"a stale failure cannot replace current state");
			loading.begin(9, 800, 600, false);
			loading.cap_need_to_source(100, 50);
			reporter.check(loading.apply(9, ui::LoadingModel::Phase::source, 100, 50) && !loading.provisional(),
			               L"a full small source settles a larger viewport request");
		}

		void test_image_resampler(Reporter& reporter)
		{
			reporter.section(L"ui::resample_bgra");
			const std::vector<std::uint32_t> source{
				0xff000000, 0xff000004, 0xff000008, 0xff00000c,
				0xff000010, 0xff000014, 0xff000018, 0xff00001c
			};
			const auto exact = ui::resample_bgra(source, {4, 2}, {0, 0, 4, 2}, {4, 2});
			reporter.check(exact == source, L"one-to-one sampling reproduces source pixels bitwise");
			const auto reduced = ui::resample_bgra(source, {4, 2}, {0, 0, 4, 2}, {2, 1});
			reporter.check(reduced.size() == 2 && reduced[0] == 0xff00000a && reduced[1] == 0xff000012,
			               L"integer downscaling area-averages each source block");
			const auto enlarged = ui::resample_bgra(source, {4, 2}, {1, 0, 1, 1}, {4, 4});
			reporter.check(enlarged.size() == 16 &&
			               std::ranges::all_of(enlarged, [](const auto pixel) { return pixel == 0xff000004; }),
			               L"high magnification preserves exact source pixels");
			const auto edge = ui::resample_bgra(source, {4, 2}, {3.5, 1.5, 2, 2}, {3, 3});
			reporter.check(edge.size() == 9, L"a source rectangle at the image edge is safely clamped");
			const std::vector<std::uint32_t> corners{
				0xff000000, 0xff0000ff,
				0xff00ff00, 0xffff0000
			};
			const auto interpolated = ui::resample_bgra(corners, {2, 2}, {0, 0, 2, 2}, {3, 3});
			reporter.check(interpolated.size() == 9,
			               L"fractional enlargement produces the requested extent");
			reporter.check(interpolated.size() == 9 && interpolated[4] == 0xff404040,
			               L"fractional enlargement bilinearly interpolates all channels");

			ui::ResampleWorkspace workspace;
			const auto first = ui::resample_bgra(source, {4, 2}, {0, 0, 4, 2}, {4, 2}, workspace);
			const auto* allocation = first.data();
			const auto second = ui::resample_bgra(source, {4, 2}, {0, 0, 4, 2}, {4, 2}, workspace);
			reporter.check(std::ranges::equal(second, source),
			               L"a retained workspace preserves exact sampling");
			reporter.check(second.data() == allocation,
			               L"a retained workspace reuses its pixel allocation");

			std::vector<std::uint32_t> cacheSource(256 * 256, 0xff123456);
			ui::ResampleCache cache;
			const auto cachedFirst = cache.resample(cacheSource, {256, 256}, {0, 0, 256, 256}, {256, 256}, 1);
			const auto cachedSecond = cache.resample(cacheSource, {256, 256}, {0, 0, 256, 256}, {256, 256}, 1);
			reporter.check(cache.hit_count() == 1 && cachedFirst.data() == cachedSecond.data(),
			               L"a repeated large resample is served from the bounded cache");
			cache.resample(cacheSource, {256, 256}, {0, 0, 256, 256}, {255, 256}, 1);
			reporter.check(cache.hit_count() == 1,
			               L"a changed destination invalidates the cached resample key");
			cache.resample(cacheSource, {256, 256}, {0, 0, 256, 256}, {256, 256}, 2);
			reporter.check(cache.hit_count() == 1,
			               L"a changed image generation cannot reuse stale cached pixels");
		}

		void test_pixel_blending(Reporter& reporter)
		{
			reporter.section(L"ui pixel blending");
			std::vector<std::uint32_t> source{
				0xff000000, 0xffffffff, 0xff123456, 0xffabcdef,
				0xff010203, 0xff102030, 0xff8090a0, 0xfffedcba,
				0xff333333, 0xff777777, 0xff2468ac
			};
			auto scalar = source;
			ui::blend_constant_bgra(scalar, color{37, 149, 231, 173}, ui::PixelBackend::scalar);
			auto sse2 = source;
			ui::blend_constant_bgra(sse2, color{37, 149, 231, 173}, ui::PixelBackend::sse2);
			reporter.check(sse2 == scalar, L"SSE2 constant-alpha blending matches the scalar reference");
			if (platform::has_avx2())
			{
				auto avx2 = source;
				ui::blend_constant_bgra(avx2, color{37, 149, 231, 173}, ui::PixelBackend::avx2);
				reporter.check(avx2 == scalar, L"AVX2 constant-alpha blending matches the scalar reference");
			}

			const std::vector<std::uint32_t> first{
				0xff000000, 0xffffffff, 0xff123456, 0xffabcdef,
				0xff010203, 0xff102030, 0xff8090a0, 0xfffedcba,
				0xff333333, 0xff777777, 0xff2468ac
			};
			const std::vector<std::uint32_t> second(first.rbegin(), first.rend());
			std::vector<std::uint32_t> scalarInterpolation(first.size());
			ui::interpolate_bgra(first, second, scalarInterpolation, 93, ui::PixelBackend::scalar);
			std::vector<std::uint32_t> sse2Interpolation(first.size());
			ui::interpolate_bgra(first, second, sse2Interpolation, 93, ui::PixelBackend::sse2);
			reporter.check(sse2Interpolation == scalarInterpolation,
			               L"SSE2 row interpolation matches the scalar reference");
			if (platform::has_avx2())
			{
				std::vector<std::uint32_t> avx2Interpolation(first.size());
				ui::interpolate_bgra(first, second, avx2Interpolation, 93, ui::PixelBackend::avx2);
				reporter.check(avx2Interpolation == scalarInterpolation,
				               L"AVX2 row interpolation matches the scalar reference");
			}
		}

		void test_folder_scan(Reporter& reporter)
		{
			reporter.section(L"files::scan_folder / read_metadata");
			std::error_code error;
			const auto root = platform::unique_temp_folder(L"iw30-tests");
			std::filesystem::remove_all(root, error);
			std::filesystem::create_directories(root, error);
			if (error)
			{
				reporter.check(false, L"test folder could be created");
				return;
			}

			const auto write = [&root](const wchar_t* name, const std::string& content)
			{
				std::ofstream stream(root / name, std::ios::binary);
				stream.write(content.data(), static_cast<std::streamsize>(content.size()));
			};
			write(L"img1.jpg", "jpeg-bytes");
			write(L"img2.png", "png-bytes");
			write(L"IMG0.BMP", "bmp-bytes");
			write(L"notes.txt", "not an image");
			std::filesystem::create_directory(root / L"subfolder", error);

			const std::stop_source stop;
			size_t reportedImages = 0;
			const auto found = files::scan_folder(root, stop.get_token(),
			                                      [&reportedImages](size_t, const size_t images)
			                                      {
				                                      reportedImages = images;
			                                      });
			reporter.check(found.size() == 3, L"scan_folder returns only supported image files");
			reporter.check(reportedImages == 3, L"scan_folder progress reports the final image count");

			const bool sorted = found.size() == 3 && found[0].filename() == L"IMG0.BMP" &&
				found[1].filename() == L"img1.jpg" && found[2].filename() == L"img2.png";
			reporter.check(sorted, L"scan_folder returns files sorted by filename");

			std::stop_source cancelled;
			cancelled.request_stop();
			reporter.check(files::scan_folder(root, cancelled.get_token()).empty(),
			               L"scan_folder honours a cancelled stop token");

			const auto metadata = files::read_metadata(root / L"img1.jpg");
			reporter.check(metadata.hasSize && metadata.size == 10, L"read_metadata returns the exact file size");
			reporter.check(metadata.hasModified, L"read_metadata returns a modified timestamp");

			std::filesystem::remove_all(root, error);
		}

		void test_folder_items(Reporter& reporter)
		{
			reporter.section(L"files::scan_folder_items / sort_items");
			std::error_code error;
			const auto root = platform::unique_temp_folder(L"iw30-item-tests");
			std::filesystem::remove_all(root, error);
			std::filesystem::create_directories(root / L"Folder", error);
			const auto write = [&root](const wchar_t* name, const std::string& content)
			{
				std::ofstream stream(root / name, std::ios::binary);
				stream.write(content.data(), static_cast<std::streamsize>(content.size()));
			};
			{
				// A 2x3 24bpp BMP written byte by byte, so the test does not depend on SDK struct layout.
				std::vector<std::uint8_t> file;
				const auto put16 = [&file](const std::uint32_t value)
				{
					file.push_back(static_cast<std::uint8_t>(value));
					file.push_back(static_cast<std::uint8_t>(value >> 8));
				};
				const auto put32 = [&put16](const std::uint32_t value)
				{
					put16(value);
					put16(value >> 16);
				};
				constexpr std::uint32_t headerBytes = 14 + 40;
				constexpr std::uint32_t pixelBytes = 24;
				put16(0x4d42);
				put32(headerBytes + pixelBytes);
				put16(0);
				put16(0);
				put32(headerBytes);
				put32(40);
				put32(2);
				put32(3);
				put16(1);
				put16(24);
				put32(0);
				put32(pixelBytes);
				put32(0);
				put32(0);
				put32(0);
				put32(0);
				file.resize(file.size() + pixelBytes);
				std::ofstream stream(root / L"photo.bmp", std::ios::binary);
				stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
			}
			write(L"notes.txt", "document");
			write(L"song.mp3", "audio-data");
			size_t imageCount = 0;
			const std::stop_source stop;
			platform::Runtime runtime(platform::RuntimeMode::multithreaded);
			auto items = files::scan_folder_items(root, stop.get_token(),
			                                      [&imageCount](size_t, const size_t images) { imageCount = images; });
			reporter.check(items.size() == 4 && imageCount == 1,
			               L"all immediate folders and files are returned while images are counted separately");
			reporter.check(items[0].kind == files::ItemKind::folder,
			               L"folders are grouped before files in the default name ordering");
			const auto photo = std::ranges::find(items, files::ItemKind::image, &files::FolderItem::kind);
			reporter.check(photo != items.end() && photo->width == 0 && photo->height == 0,
			               L"folder scanning defers image inspection to media workers");
			reporter.check(files::classify(L"movie.mp4") == files::ItemKind::video &&
			               files::classify(L"song.mp3") == files::ItemKind::audio &&
			               files::classify(L"notes.txt") == files::ItemKind::document,
			               L"common non-image file types are classified for visual treatment");
			files::sort_items(items, files::SortField::size);
			reporter.check(
				items.front().kind == files::ItemKind::folder && items.back().path.filename() == L"photo.bmp",
				L"size sorting keeps folders first and orders files by byte size");
			files::sort_items(items, files::SortField::dimensions);
			reporter.check(items.front().kind == files::ItemKind::folder && items.size() == 4,
			               L"dimensions sorting remains stable while image dimensions are deferred");
			std::filesystem::remove_all(root, error);
		}

		void test_image_downsample(Reporter& reporter)
		{
			reporter.section(L"files::downsample_bgra_2x");
			const files::DecodedImage source{
				4, 2, {
					0xff000000, 0xff000004, 0xff000008, 0xff00000c,
					0xff000010, 0xff000014, 0xff000018, 0xff00001c
				}
			};
			const auto reduced = files::downsample_bgra_2x(source);
			reporter.check(reduced.width == 2 && reduced.height == 1, L"a 4 x 2 image becomes 2 x 1");
			reporter.check(reduced.pixels.size() == 2 && reduced.pixels[0] == 0xff00000a &&
			               reduced.pixels[1] == 0xff000012,
			               L"each output pixel averages its four BGRA source pixels");

			const files::DecodedImage odd{3, 1, {0xff000000, 0xff000004, 0xff112233}};
			const auto oddReduced = files::downsample_bgra_2x(odd);
			reporter.check(oddReduced.width == 2 && oddReduced.height == 1,
			               L"odd dimensions retain the final edge pixel");
			reporter.check(oddReduced.pixels.size() == 2 && oddReduced.pixels[1] == 0xff112233,
			               L"an unpaired edge is replicated without changing its color");
		}

		void test_image_save_options(Reporter& reporter)
		{
			reporter.section(L"files image save options");
			reporter.check(std::wstring_view(files::image_save_extension(files::ImageSaveFormat::png)) == L".png" &&
			               std::wstring_view(files::image_save_extension(files::ImageSaveFormat::jpeg)) == L".jpg",
			               L"save formats expose stable filename extensions");
			std::error_code error;
			const auto root = platform::unique_temp_folder(L"iw30-save-tests");
			std::filesystem::create_directories(root, error);
			std::ofstream(root / L"New 1.png", std::ios::binary).put('x');
			reporter.check(files::next_image_path(root, files::ImageSaveFormat::png).filename() == L"New 2.png",
			               L"new image naming skips existing files without overwriting them");
			const auto formats = files::writable_image_formats();
			reporter.check(!formats.empty(), L"at least one installed WIC image encoder is available");
			const files::DecodedImage image{
				2, 2,
				{0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff}, 2, 2
			};
			for (const auto format : formats)
			{
				const auto output = root / (std::wstring(L"encoded") + files::image_save_extension(format));
				reporter.check(
					files::save_image(image, output, format) && std::filesystem::file_size(output, error) > 0,
					L"each advertised WIC format encodes a non-empty image file");
			}
			const auto occupied = root / L"occupied.png";
			std::ofstream(occupied, std::ios::binary) << "original";
			reporter.check(!files::save_image(image, occupied, files::ImageSaveFormat::png, {}, false),
				L"a new-output save never overwrites an occupied destination");
			const auto read = [](const std::filesystem::path& path)
			{
				std::ifstream stream(path, std::ios::binary);
				return std::string(std::istreambuf_iterator<char>(stream), {});
			};
			reporter.check(read(occupied) == "original",
				L"a refused image commit retains every original destination byte");
			reporter.check(!files::save_image({}, occupied, files::ImageSaveFormat::png) &&
				read(occupied) == "original", L"an invalid image cannot truncate an existing destination");
			std::filesystem::create_directory(root / L"directory.png", error);
			reporter.check(!files::save_image(image, root / L"directory.png", files::ImageSaveFormat::png) &&
				std::filesystem::is_directory(root / L"directory.png"),
				L"a failed image commit cannot replace a directory");
			reporter.check(std::ranges::none_of(std::filesystem::directory_iterator(root), [](const auto& entry)
			{
				return entry.path().filename().wstring().starts_with(L".iw30-");
			}), L"failed image commits remove their incomplete temporary output");
			reporter.check(files::save_image(image, occupied, files::ImageSaveFormat::png) &&
				files::load_image(occupied).pixels == image.pixels,
				L"an explicitly approved replacement publishes a complete decodable image");
			const auto snapshot = files::snapshot_file(occupied);
			reporter.check(snapshot && snapshot->exists && files::matches_snapshot(occupied, *snapshot),
				L"a readable file has a matching identity snapshot");
			bool commitChecked = false;
			const auto originalBytes = read(occupied);
			reporter.check(!files::save_image(image, occupied, files::ImageSaveFormat::png, {}, true, [&]
			{
				commitChecked = true;
				return false;
			}) && commitChecked && read(occupied) == originalBytes,
				L"a rejected final revalidation preserves the existing file after encoding");
			const auto absent = files::snapshot_file(root / L"appeared.png");
			reporter.check(absent && !absent->exists, L"a missing file has an explicit absent snapshot");
			reporter.check(!files::save_image(image, root / L"appeared.png", files::ImageSaveFormat::png, {}, false, [&]
			{
				std::ofstream(root / L"appeared.png") << "unreviewed";
				return true;
			}) && read(root / L"appeared.png") == "unreviewed",
				L"an output created at commit time is not overwritten");
			auto longFolder = root;
			// The legacy Windows path limit that files::native_path exists to escape.
			constexpr size_t legacyPathLimit = 260;
			while (longFolder.native().size() <= legacyPathLimit + 32)
				longFolder /= L"long-path-segment-0123456789abcdef0123456789";
			std::filesystem::create_directories(files::native_path(longFolder), error);
			const auto longOutput = longFolder / L"encoded.png";
			const bool longSaved = !error && files::save_image(image, longOutput, files::ImageSaveFormat::png);
			const auto longLoaded = longSaved ? files::load_image(longOutput) : files::DecodedImage{};
			reporter.check(!error, L"directories longer than MAX_PATH can be created");
			reporter.check(longSaved, L"WIC save supports paths longer than MAX_PATH");
			reporter.check(longLoaded.width == 2 && longLoaded.height == 2,
			               L"WIC decode supports paths longer than MAX_PATH");
			std::filesystem::remove_all(root, error);
		}

		tasks::TaskPlan make_plan(const std::vector<std::pair<std::wstring, std::wstring>>& rows)
		{
			tasks::TaskPlan plan;
			plan.generation = 1;
			for (const auto& [source, destination] : rows)
			{
				tasks::TaskRow row;
				row.source = source;
				row.destination = destination;
				plan.rows.push_back(std::move(row));
			}
			return plan;
		}

		// A pure Auto-rename generator, so plan tests never depend on the filesystem.
		tasks::UniqueFunction make_unique_names()
		{
			return [](const std::filesystem::path& proposed, const tasks::ExistsFunction& taken)
			{
				for (int number = 2; number < 64; ++number)
				{
					auto candidate = proposed.parent_path() /
						std::format(L"{} ({}){}", proposed.stem().wstring(), number,
						            proposed.extension().wstring());
					if (!taken(candidate)) return candidate;
				}
				return std::filesystem::path{};
			};
		}

		void test_task_plan(Reporter& reporter)
		{
			reporter.section(L"tasks::TaskPlan collision policy");
			const auto unique = make_unique_names();
			const tasks::ExistsFunction nothingExists = [](const std::filesystem::path&) { return false; };
			const tasks::ExistsFunction oneExists = [](const std::filesystem::path& path)
			{
				return path.filename() == L"b.jpg";
			};

			auto plan = make_plan({{L"in\\a.jpg", L"out\\a.jpg"}, {L"in\\b.jpg", L"out\\b.jpg"}});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::block, nothingExists, unique);
			reporter.check(plan.can_run() && plan.count(tasks::RowState::ready) == 2 && plan.collisions == 0,
			               L"a plan with free destinations is ready to run");

			plan = make_plan({{L"in\\a.jpg", L"out\\a.jpg"}, {L"in\\b.jpg", L"out\\b.jpg"}});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::block, oneExists, unique);
			reporter.check(!plan.can_run() && plan.collisions == 1 &&
			               plan.rows[1].state == tasks::RowState::blocked,
			               L"Block run refuses a plan whose destination already exists, with a reason");

			plan = make_plan({{L"in\\a.jpg", L"out\\a.jpg"}, {L"in\\b.jpg", L"out\\b.jpg"}});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::skip, oneExists, unique);
			reporter.check(plan.can_run() && plan.skips == 1 && plan.rows[1].state == tasks::RowState::skipped,
			               L"Skip leaves the occupied destination alone and still allows Run");

			plan = make_plan({{L"in\\a.jpg", L"out\\a.jpg"}, {L"in\\b.jpg", L"out\\b.jpg"}});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::autoRename, oneExists, unique);
			reporter.check(plan.can_run() && plan.rows[1].destination.filename() == L"b (2).jpg" &&
			               plan.rows[1].state == tasks::RowState::ready,
			               L"Auto-rename shows the free destination it chose in Review");

			plan = make_plan({{L"in\\a.jpg", L"out\\a.jpg"}, {L"in\\b.jpg", L"out\\b.jpg"}});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::replace, oneExists, unique);
			reporter.check(plan.can_run() && plan.replacements == 1 && plan.rows[1].change == L"Replace",
			               L"Replace identifies the overwritten destination in Review");

			plan = make_plan({{L"in\\a.jpg", L"out\\same.jpg"}, {L"in\\b.jpg", L"out\\same.jpg"}});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::autoRename, nothingExists, unique);
			reporter.check(plan.can_run() && plan.rows[1].destination.filename() == L"same (2).jpg",
			               L"a destination claimed twice within one plan is a collision Auto-rename resolves");

			for (const auto policy : {
				     tasks::CollisionPolicy::block, tasks::CollisionPolicy::skip,
				     tasks::CollisionPolicy::autoRename, tasks::CollisionPolicy::replace
			     })
			{
				auto merged = make_plan({{L"in\\a.jpg", L"out\\same.jpg"}, {L"in\\b.jpg", L"out\\same.jpg"}});
				tasks::resolve_collisions(merged, policy, nothingExists, unique);
				reporter.check(tasks::duplicate_destination(merged).empty(),
				               L"no policy leaves two sources merged into one destination");
				const bool resolvable = policy == tasks::CollisionPolicy::autoRename ||
					policy == tasks::CollisionPolicy::skip;
				reporter.check(merged.can_run() == resolvable,
				               L"Block run and Replace refuse two rows claiming one name");
			}

			auto forced = make_plan({{L"in\\a.jpg", L"out\\same.jpg"}, {L"in\\b.jpg", L"out\\same.jpg"}});
			for (auto& row : forced.rows) row.state = tasks::RowState::ready;
			reporter.check(tasks::block_duplicate_destinations(forced) && !forced.can_run(),
			               L"a finished plan holding a merged destination is blocked whatever produced it");

			plan = make_plan({{L"in\\a.jpg", L"out\\a.jpg"}});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::skip, [](const std::filesystem::path&)
			{
				return true;
			}, unique);
			reporter.check(!plan.can_run() && plan.blockReason == L"Every item would be skipped.",
			               L"an all-skipped plan is blocked with a stated reason");

			plan = make_plan({});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::block, nothingExists, unique);
			reporter.check(!plan.can_run(), L"an empty plan cannot run");

			plan = make_plan({{L"in\\a.jpg", L""}});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::autoRename, nothingExists, unique);
			reporter.check(!plan.can_run() && plan.rows[0].state == tasks::RowState::blocked,
			               L"a row without a destination blocks the plan");

			plan = make_plan({{L"in\\a.jpg", L"out\\a.jpg"}, {L"in\\b.jpg", L"out\\b.jpg"}});
			plan.rows[0].state = tasks::RowState::blocked;
			plan.rows[0].change = L"Invalid source";
			plan.rows[0].detail = L"The source cannot be read.";
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::replace, nothingExists, unique);
			reporter.check(!plan.can_run() && plan.rows[0].state == tasks::RowState::blocked &&
				plan.rows[0].change == L"Invalid source" && plan.rows[0].detail == L"The source cannot be read.",
				L"collision resolution preserves an analyzer's blocked row and its actionable reason");

			plan = make_plan({{L"in\\a.jpg", L"out\\a.jpg"}, {L"in\\b.jpg", L"out\\b.jpg"}});
			plan.rows[0].state = tasks::RowState::skipped;
			plan.rows[0].change = L"Already at destination";
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::block, nothingExists, unique);
			reporter.check(plan.can_run() && plan.skips == 1 && plan.rows[0].state == tasks::RowState::skipped,
				L"collision resolution never resurrects a reviewed no-op");

			plan = make_plan({{L"in\\a.jpg", L"out\\b.jpg"}});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::autoRename, oneExists,
				[](const std::filesystem::path& proposed, const tasks::ExistsFunction&) { return proposed; });
			reporter.check(!plan.can_run() && plan.rows[0].state == tasks::RowState::blocked,
				L"a faulty unique-name provider cannot approve an occupied destination");

			plan = make_plan({{L"in\\a.jpg", L"out\\same.jpg"}, {L"in\\b.jpg", L"OUT\\.\\same.jpg"}});
			tasks::resolve_collisions(plan, tasks::CollisionPolicy::replace, nothingExists, unique);
			reporter.check(!plan.can_run() && plan.rows[1].state == tasks::RowState::blocked,
				L"case and dot-segment aliases cannot claim one destination twice");

			const auto snapshot = std::make_shared<const std::wstring>(L"reviewed source state");
			plan.rows[0].context = snapshot;
			const auto copiedPlan = plan;
			reporter.check(copiedPlan.rows[0].context == snapshot &&
				*std::static_pointer_cast<const std::wstring>(copiedPlan.rows[0].context) == L"reviewed source state",
				L"review and worker copies share the same immutable workflow snapshot");
		}

		void test_task_run_plan(Reporter& reporter)
		{
			reporter.section(L"tasks::run_plan");
			const auto ready_plan = [](const size_t count)
			{
				tasks::TaskPlan plan;
				for (size_t index = 0; index < count; ++index)
				{
					tasks::TaskRow row;
					row.source = std::format(L"in\\{}.jpg", index);
					row.destination = std::format(L"out\\{}.jpg", index);
					row.state = tasks::RowState::ready;
					plan.rows.push_back(std::move(row));
				}
				return plan;
			};

			// One malformed item fails one row: rows after the throw must still be attempted.
			auto plan = ready_plan(5);
			size_t acted = 0;
			const std::stop_source running;
			tasks::run_plan(plan, running.get_token(), {
				                {}, [&acted](tasks::TaskRow& row)
				                {
					                ++acted;
					                if (row.source.filename() == L"1.jpg") throw std::runtime_error("bad file");
				                },
				                {}
			                });
			reporter.check(acted == 5 && plan.count(tasks::RowState::success) == 4 &&
			               plan.count(tasks::RowState::failed) == 1,
			               L"a row that throws fails alone and the following rows still run");
			reporter.check(plan.rows[1].detail == L"bad file",
			               L"the failing row keeps its actionable error text");

			plan = ready_plan(5);
			std::stop_source cancelling;
			size_t completed = 0, reportedTotal = 0;
			tasks::run_plan(plan, cancelling.get_token(), {
				                {}, [&cancelling](const tasks::TaskRow& row)
				                {
					                if (row.source.filename() == L"1.jpg") cancelling.request_stop();
				                },
				                [&completed, &reportedTotal](const size_t complete, const size_t total)
				                {
					                completed = complete;
					                reportedTotal = total;
				                }
			                });
			reporter.check(plan.count(tasks::RowState::success) == 2 && plan.count(tasks::RowState::notRun) == 3,
			               L"cancel stops future rows and leaves them Not run, not Success");
			reporter.check(completed == 2 && reportedTotal == 5,
			               L"progress carries a completed position and a known total");

			plan = ready_plan(3);
			tasks::run_plan(plan, running.get_token(), {
				                [](const tasks::TaskRow& row) { return row.source.filename() != L"1.jpg"; },
				                [](tasks::TaskRow&)
				                {
				                },
				                {}
			                });
			reporter.check(plan.rows[1].state == tasks::RowState::failed && !plan.rows[1].detail.empty() &&
			               plan.rows[2].state == tasks::RowState::success,
			               L"a row that changed since Review is refused while independent rows continue");

			plan = ready_plan(2);
			plan.rows[0].state = tasks::RowState::skipped;
			tasks::run_plan(plan, running.get_token(), {{}, [](tasks::TaskRow&) {}, {}});
			reporter.check(plan.rows[0].state == tasks::RowState::skipped,
			               L"a skipped row keeps its reviewed outcome through a run");
			reporter.check(!tasks::summarize_results(plan).empty(),
			               L"a finished run summarises its counts");

			plan = ready_plan(4);
			plan.rows[0].state = tasks::RowState::skipped;
			plan.rows[2].state = tasks::RowState::skipped;
			std::vector<size_t> activeRows;
			std::vector<size_t> completedRows;
			tasks::RunOptions observed;
			observed.act = [](tasks::TaskRow&) {};
			observed.rowProgress = [&activeRows, &completedRows](const size_t index, const tasks::TaskRow& item)
			{
				if (item.state == tasks::RowState::running) activeRows.push_back(index);
				else if (item.state == tasks::RowState::success) completedRows.push_back(index);
			};
			tasks::run_plan(plan, running.get_token(), observed);
			reporter.check(activeRows == std::vector<size_t>{1, 3} && completedRows == activeRows,
				L"row progress identifies actual reviewed indices even when skipped rows precede the current item");

			plan = ready_plan(3);
			observed.progress = [](size_t, size_t) { throw std::runtime_error("broken progress sink"); };
			observed.rowProgress = [](size_t, const tasks::TaskRow&) { throw std::runtime_error("broken row sink"); };
			bool reportedWithoutThrow = true;
			try { tasks::run_plan(plan, running.get_token(), observed); }
			catch (...) { reportedWithoutThrow = false; }
			reporter.check(reportedWithoutThrow && plan.count(tasks::RowState::success) == 3,
				L"a throwing progress observer cannot abandon independent rows or their final outcomes");

			plan = ready_plan(1);
			tasks::run_plan(plan, running.get_token(), {});
			reporter.check(plan.rows[0].state == tasks::RowState::failed && !plan.rows[0].detail.empty(),
				L"a missing operation is never reported as a successful write");

			plan = ready_plan(3);
			plan.rows[0].state = tasks::RowState::pending;
			plan.rows[1].state = tasks::RowState::blocked;
			plan.rows[1].change = L"Source not readable";
			tasks::run_plan(plan, running.get_token(), {{}, [](tasks::TaskRow& item)
			{
				item.state = tasks::RowState::ready;
			}, {}});
			reporter.check(plan.count(tasks::RowState::notRun) == 1 && plan.count(tasks::RowState::failed) == 2 &&
				tasks::summarize_results(plan).find(L"Source not readable") != std::wstring::npos,
				L"every final row has an outcome even when input or an operation leaves an incomplete state");

			plan = ready_plan(4);
			std::stop_source finishCancel;
			int finalizations = 0;
			tasks::RunOptions finalizing;
			finalizing.act = [&finishCancel](const tasks::TaskRow& item)
			{
				if (item.source.filename() == L"1.jpg") finishCancel.request_stop();
			};
			finalizing.finish = [&finalizations](tasks::TaskPlan& result)
			{
				++finalizations;
				if (result.rows[2].state == tasks::RowState::notRun)
				{
					result.rows[2].state = tasks::RowState::failed;
					result.rows[2].detail = L"Could not restore the staged source.";
				}
			};
			tasks::run_plan(plan, finishCancel.get_token(), finalizing);
			reporter.check(finalizations == 1 && plan.count(tasks::RowState::success) == 2 &&
				plan.count(tasks::RowState::failed) == 1 && plan.count(tasks::RowState::notRun) == 1 &&
				tasks::summarize_results(plan).find(L"restore the staged source") != std::wstring::npos,
				L"cancel finalization runs once before publishing and retains per-row recovery failures");

			plan = ready_plan(1);
			finalizing.act = [](tasks::TaskRow&) {};
			finalizing.finish = [](tasks::TaskPlan&) { throw std::runtime_error("staging cleanup failed"); };
			tasks::run_plan(plan, running.get_token(), finalizing);
			reporter.check(plan.rows[0].state == tasks::RowState::success && !plan.completionError.empty() &&
				tasks::summarize_results(plan).find(L"staging cleanup failed") != std::wstring::npos,
				L"an unexpected finalization exception preserves actual row results and reports its completion error");

			plan = ready_plan(1);
			plan.rows[0].state = tasks::RowState::notRun;
			plan.rows[0].detail = L"The staged source needs attention at its recovery path.";
			reporter.check(tasks::summarize_results(plan).find(plan.rows[0].detail) != std::wstring::npos,
				L"Not run recovery details remain visible in the summary when no failed-row error takes precedence");
		}

		void test_task_runner(Reporter& reporter)
		{
			reporter.section(L"tasks::TaskRunner");
			std::vector<std::function<void()>> background;
			std::vector<std::function<void()>> ui;
			tasks::TaskRunner::Dispatcher dispatcher;
			dispatcher.background = [&background](std::function<void()> work)
			{
				background.push_back(std::move(work));
				return true;
			};
			dispatcher.ui = [&ui](std::function<void()> work)
			{
				ui.push_back(std::move(work));
				return true;
			};
			const auto drain = [](std::vector<std::function<void()>>& queue)
			{
				std::vector<std::function<void()>> batch;
				batch.swap(queue);
				for (auto& work : batch) work();
			};

			tasks::TaskRunner runner(dispatcher);
			int analyzed = 0;
			runner.analyzed = [&analyzed](tasks::TaskPlan) { ++analyzed; };

			runner.analyze([](const std::stop_token&, const tasks::ProgressFunction&)
			{
				tasks::TaskPlan plan;
				return plan;
			});
			reporter.check(runner.busy() && runner.phase() == tasks::TaskRunner::Phase::analyzing,
			               L"an analysis in flight reports the analyzing phase");
			drain(background);
			drain(ui);
			reporter.check(analyzed == 1 && !runner.busy(), L"a completed analysis returns the runner to idle");

			const auto generation = runner.generation();
			runner.analyze([](const std::stop_token&, const tasks::ProgressFunction&) { return tasks::TaskPlan{}; });
			runner.invalidate();
			drain(background);
			drain(ui);
			reporter.check(analyzed == 1, L"a completion carrying a stale generation is dropped, never merged");
			reporter.check(runner.generation() > generation && !runner.busy(),
			               L"invalidate bumps the generation and clears the phase");

			// Stopped and started again: the runner must still accept and complete work.
			runner.analyze([](const std::stop_token&, const tasks::ProgressFunction&) { return tasks::TaskPlan{}; });
			drain(background);
			drain(ui);
			reporter.check(analyzed == 2, L"a runner that was invalidated still processes later work");

			tasks::TaskPlan stale;
			stale.generation = 1;
			tasks::TaskRow row;
			row.destination = L"out\\a.jpg";
			row.state = tasks::RowState::ready;
			stale.rows.push_back(row);
			reporter.check(!runner.run(stale, {}), L"a run against a superseded plan is refused outright");

			auto current = stale;
			current.generation = runner.generation();
			int finished = 0;
			runner.finished = [&finished](tasks::TaskPlan) { ++finished; };
			reporter.check(runner.run(current, {{}, [](tasks::TaskRow&) {}, {}}),
			               L"a run against the current generation is accepted");
			runner.cancel();
			drain(background);
			drain(ui);
			reporter.check(finished == 1,
			               L"a cancelled run still reports its partial result instead of vanishing");

			tasks::TaskPlan blocked;
			blocked.blockReason = L"no destination";
			reporter.check(!runner.run(blocked, {}), L"a blocked plan cannot be run");

			current.generation = 0;
			reporter.check(!runner.run(current, {{}, [](tasks::TaskRow&) {}, {}}),
				L"an unstamped plan cannot bypass the reviewed generation check");
			current.generation = runner.generation();
			reporter.check(runner.run(current, {{}, [](tasks::TaskRow&) {}, {}}) &&
				!runner.run(current, {{}, [](tasks::TaskRow&) {}, {}}) &&
				!runner.analyze([](const std::stop_token&, const tasks::ProgressFunction&) { return tasks::TaskPlan{}; }),
				L"reentrant Run and Analyze cannot replace an in-flight write");
			drain(background);
			drain(ui);

			tasks::TaskPlan cancelledAnalysis;
			runner.analyzed = [&cancelledAnalysis](tasks::TaskPlan plan) { cancelledAnalysis = std::move(plan); };
			bool beganCancelledAnalysis = false;
			runner.analyze([&beganCancelledAnalysis, row](const std::stop_token&, const tasks::ProgressFunction&)
			{
				beganCancelledAnalysis = true;
				tasks::TaskPlan plan;
				plan.rows.push_back(row);
				return plan;
			});
			runner.cancel();
			drain(background);
			drain(ui);
			reporter.check(!beganCancelledAnalysis && !cancelledAnalysis.can_run() && !runner.busy(),
				L"cancelling before analysis starts skips its scan and still completes with a non-runnable result");

			runner.analyze([row](const std::stop_token&, const tasks::ProgressFunction&)
			{
				tasks::TaskPlan plan;
				plan.rows.push_back(row);
				return plan;
			});
			drain(background);
			runner.cancel();
			drain(ui);
			reporter.check(!cancelledAnalysis.can_run() &&
				cancelledAnalysis.blockReason.find(L"cancelled") != std::wstring::npos,
				L"cancel while an analysis completion is queued cannot enable Run on that partial review");

			int acceptedGeneration = 0, progressReports = 0;
			runner.analyzed = [&acceptedGeneration](tasks::TaskPlan plan)
			{
				acceptedGeneration = plan.rows.empty() ? 0 : plan.rows.front().tag;
			};
			runner.progress = [&progressReports](tasks::TaskRunner::Phase, size_t, size_t) { ++progressReports; };
			runner.analyze([row](const std::stop_token&, const tasks::ProgressFunction& report) mutable
			{
				report(1, 1);
				tasks::TaskPlan plan;
				row.tag = 1;
				plan.rows.push_back(row);
				return plan;
			});
			drain(background);
			runner.invalidate();
			runner.analyze([row](const std::stop_token&, const tasks::ProgressFunction& report) mutable
			{
				report(1, 1);
				tasks::TaskPlan plan;
				row.tag = 2;
				plan.rows.push_back(row);
				return plan;
			});
			drain(background);
			drain(ui);
			reporter.check(acceptedGeneration == 2 && progressReports == 1 && !runner.busy(),
				L"queued stale progress and completion cannot replace a newer review");

			int chained = 0;
			runner.analyzed = [&runner, &chained](tasks::TaskPlan)
			{
				if (++chained == 1)
					runner.analyze([](const std::stop_token&, const tasks::ProgressFunction&) { return tasks::TaskPlan{}; });
			};
			runner.analyze([](const std::stop_token&, const tasks::ProgressFunction&) { return tasks::TaskPlan{}; });
			drain(background);
			drain(ui);
			drain(background);
			drain(ui);
			reporter.check(chained == 2 && !runner.busy(),
				L"an analysis completion can schedule the next request without that request being retired");

			bool rejectWork = true;
			auto rejectingDispatcher = dispatcher;
			rejectingDispatcher.background = [&rejectWork, &background](std::function<void()> work)
			{
				if (rejectWork) return false;
				background.push_back(std::move(work));
				return true;
			};
			tasks::TaskRunner restartable(rejectingDispatcher);
			reporter.check(!restartable.analyze([](const std::stop_token&, const tasks::ProgressFunction&)
				{ return tasks::TaskPlan{}; }) && !restartable.busy(),
				L"a rejected work queue does not strand the runner in Analyzing");
			rejectWork = false;
			int resumed = 0;
			restartable.analyzed = [&resumed](tasks::TaskPlan) { ++resumed; };
			restartable.analyze([](const std::stop_token&, const tasks::ProgressFunction&) { return tasks::TaskPlan{}; });
			drain(background);
			drain(ui);
			reporter.check(resumed == 1 && !restartable.busy(),
				L"the same runner can retry successfully after its queue refused a request");

			int postDestructionCallbacks = 0;
			{
				auto destroyed = std::make_unique<tasks::TaskRunner>(dispatcher);
				destroyed->analyzed = [&postDestructionCallbacks](tasks::TaskPlan) { ++postDestructionCallbacks; };
				destroyed->analyze([](const std::stop_token&, const tasks::ProgressFunction&) { return tasks::TaskPlan{}; });
				drain(background);
			}
			drain(ui);
			reporter.check(postDestructionCallbacks == 0,
				L"a completion queued before runner destruction never calls its dead owner");

			bool finalizedOnWorker = false, publishedFinalizedResult = false;
			tasks::RunOptions finishBeforePublish;
			finishBeforePublish.act = [](tasks::TaskRow&) {};
			finishBeforePublish.finish = [&finalizedOnWorker](tasks::TaskPlan& result)
			{
				finalizedOnWorker = true;
				result.rows.front().state = tasks::RowState::failed;
				result.rows.front().detail = L"Recovery failed";
			};
			runner.finished = [&finalizedOnWorker, &publishedFinalizedResult](const tasks::TaskPlan& result)
			{
				publishedFinalizedResult = finalizedOnWorker &&
					result.rows.front().state == tasks::RowState::failed;
			};
			current.generation = runner.generation();
			runner.run(current, finishBeforePublish);
			drain(background);
			reporter.check(finalizedOnWorker && !publishedFinalizedResult,
				L"staging finalization completes on the worker before a result is delivered to the UI");
			drain(ui);
			reporter.check(publishedFinalizedResult && !runner.busy(),
				L"TaskRunner publishes the finalized recovery outcomes rather than a pre-cleanup snapshot");

			class TestFrame final : public platform::WindowFrame, public std::enable_shared_from_this<TestFrame>
			{
			public:
				bool retainReactor{};
				int closes{};
				platform::FrameReactorPtr reactor;
				platform::NativeHandle native_handle() const override { return {}; }
				void set_reactor(platform::FrameReactorPtr value) override { reactor = std::move(value); }
				platform::WindowFramePtr create_child(platform::FrameReactorPtr value, const platform::WindowOptions&) override
				{
					if (retainReactor) set_reactor(std::move(value));
					return shared_from_this();
				}
				recti client_rect() const override { return {0, 0, 800, 600}; }
				void move(recti) override {}
				void show(bool) override {}
				void invalidate() override {}
				void invalidate(recti) override {}
				void set_focus() override {}
				bool has_focus() const override { return true; }
				void set_capture() override {}
				void release_capture() override {}
				void track_mouse_leave() override {}
				void configure_gestures(bool, bool) override {}
				void start_timer(unsigned int) override {}
				void stop_timer() override {}
				void accept_file_drops(bool) override {}
				void set_cursor(platform::CursorShape) override {}
				pointi screen_to_client(pointi point) const override { return point; }
				pointi client_to_screen(pointi point) const override { return point; }
				void set_title(std::wstring_view) override {}
				void set_fullscreen(bool) override {}
				void set_maximized(bool) override {}
				platform::WindowPlacement placement() const override { return {}; }
				void set_accelerators(std::span<const platform::CommandAccelerator>) override {}
				void close() override { ++closes; }
				unsigned int dpi() const override { return 96; }
			};
			class TestView final : public TaskView
			{
			public:
				explicit TestView(tasks::TaskRunner::Dispatcher value) : TaskView(std::move(value)) {}
				std::wstring title() const override { return L"Test task"; }
				void change() { controls_changed(); }
				platform::TextInputOptions text_options(const int id)
				{
					ui::Control control;
					control.id = id;
					control.kind = ui::ControlKind::text;
					panel_.add(std::move(control));
					platform::TextInputOptions options;
					bind_text_input(id, options);
					return options;
				}
				int focused_control() const { return panel_.focused_id(); }
				void focus_control(const int id) { panel_.focus(id); }
				void rebuild_for_test() { rebuild_controls(); }
				std::wstring cell(const tasks::TaskRow& row, const size_t column) const
					{ return review_cell(row, column); }
				std::vector<ui::Control> controlsToBuild;
				bool confirmClose{true};
				bool automatic{true};
				std::wstring analysisProblem;
				std::wstring customSummary;
				std::wstring outcomeText;
				int prompts{};
				int analyses{};
				int actions{};
				bool controlsLocked{};
			protected:
				void build_controls() override
				{
					for (const auto& control : controlsToBuild) panel_.add(control);
				}
				bool auto_analyze() const override { return automatic; }
				std::wstring analysis_block_reason() const override { return analysisProblem; }
				std::wstring analysis_summary() const override
					{ return customSummary.empty() ? TaskView::analysis_summary() : customSummary; }
				std::vector<Column> review_columns() const override { return {{L"Source"}, {L"Target"}, {L"Status"}}; }
				tasks::TaskRunner::AnalyzeFunction make_analyzer() override
				{
					return [revision = ++analyses](const std::stop_token&, const tasks::ProgressFunction& report)
					{
						tasks::TaskPlan plan;
						for (int index = 0; index < 2; ++index)
						{
							tasks::TaskRow item;
							item.source = std::format(L"in\\{}.jpg", index);
							item.destination = std::format(L"out\\{}.jpg", index);
							item.state = tasks::RowState::ready;
							item.tag = revision;
							plan.rows.push_back(std::move(item));
						}
						report(2, 2);
						return plan;
					};
				}
				tasks::RunOptions build_run_options() override
					{ return {{}, [this](tasks::TaskRow& row) { ++actions; row.change = outcomeText; }, {}}; }
				void work_state_changed(const bool active) override { controlsLocked = active; }
				bool confirm_cancel_close() override { ++prompts; return confirmClose; }
			};
			const auto testFrame = std::make_shared<TestFrame>();
			auto view = std::make_shared<TestView>(dispatcher);
			int closedViews = 0;
			int completedViews = 0;
			tasks::TaskPlan completedViewPlan;
			bool maximized = false;
			std::wstring viewStatus;
			TaskView::Host host;
			host.closed = [&closedViews] { ++closedViews; };
			host.status = [&viewStatus](std::wstring text) { viewStatus = std::move(text); };
			host.completed = [&completedViews, &completedViewPlan](const tasks::TaskPlan& plan)
			{
				++completedViews;
				completedViewPlan = plan;
			};
			host.toggleMaximize = [&maximized] { maximized = !maximized; };
			host.maximized = [&maximized] { return maximized; };
			view->create(testFrame, host);
			auto toolbar = view->toolbar_items();
			reporter.check(!view->plan().can_run() && !toolbar.front().command->enabled(),
				L"a newly opened task cannot run an empty, unreviewed plan");
			reporter.check(toolbar.back().command->name == L"Close" &&
				toolbar[toolbar.size() - 2].command->toolbarText() == L"Maximize",
				L"the task toolbar ends with Maximize and Close");
			toolbar[toolbar.size() - 2].command->invoke();
			reporter.check(maximized && toolbar[toolbar.size() - 2].command->toolbarText() == L"Restore",
				L"the task's window action toggles the host and displays Restore when maximized");
			view->analyze();
			reporter.check(view->busy() && !toolbar.front().command->enabled(),
				L"Run is disabled throughout analysis, including before a worker starts");
			drain(background);
			view->change();
			drain(background);
			drain(ui);
			reporter.check(!view->busy() && view->plan().rows.front().tag == 2 &&
				toolbar.front().command->enabled(),
				L"a control change replaces a queued analysis result with the new generation");

			view->start_run();
			reporter.check(!view->dispose() && view->frame(),
				L"a task cannot dispose its native surface while reviewed work is still active");
			view->confirmClose = false;
			reporter.check(view->controlsLocked && !view->request_close() && view->busy() && !view->closing(),
				L"Keep running vetoes close and leaves the immutable running controls locked");
			view->confirmClose = true;
			reporter.check(!view->request_close() && closedViews == 0 && view->busy() && view->closing(),
				L"Cancel operation defers closing until the worker reports its partial results");
			const int promptCount = view->prompts;
			reporter.check(!view->request_close() && view->prompts == promptCount,
				L"repeated Close while cancellation is pending neither prompts again nor abandons the job");
			drain(background);
			drain(ui);
			reporter.check(closedViews == 1 && !view->busy() && !view->closing() && !view->controlsLocked &&
				view->plan().count(tasks::RowState::notRun) == 2 && view->actions == 0 &&
				viewStatus.find(L"2 not run") != std::wstring::npos,
				L"cancel-and-close retains Not run outcomes and closes exactly once after completion");
			reporter.check(completedViews == 1 && completedViewPlan.count(tasks::RowState::notRun) == 2,
				L"the host receives the complete partial outcome before cancellation closes the task");

			auto outcomeView = std::make_shared<TestView>(dispatcher);
			outcomeView->outcomeText = L"Skipped because the destination appeared after Review";
			outcomeView->create(testFrame, {});
			outcomeView->analyze();
			drain(background);
			drain(ui);
			outcomeView->start_run();
			drain(background);
			drain(ui);
			reporter.check(outcomeView->cell(outcomeView->plan().rows.front(), 2).find(outcomeView->outcomeText) !=
				std::wstring::npos,
				L"the result column retains each row's workflow-specific outcome after processing");

			auto cancelView = std::make_shared<TestView>(dispatcher);
			int cancelClosed = 0;
			TaskView::Host cancelHost;
			cancelHost.closed = [&cancelClosed] { ++cancelClosed; };
			cancelView->create(testFrame, std::move(cancelHost));
			const auto installedToolbar = cancelView->toolbar_items();
			const auto cancelItem = std::ranges::find_if(installedToolbar, [](const platform::ToolbarItem& item)
			{
				return item.command && item.command->name == L"Cancel";
			});
			const auto installedCancel = cancelItem == installedToolbar.end() ? platform::CommandPtr{} : cancelItem->command;
			reporter.check(installedCancel && !installedCancel->enabled(),
				L"the idle task installs a disabled Cancel button before analysis starts");
			cancelView->analyze();
			reporter.check(installedCancel && installedCancel->enabled(),
				L"the installed Cancel button enables for analysis without rebuilding the toolbar");
			if (installedCancel) installedCancel->invoke();
			drain(background);
			drain(ui);
			reporter.check(!cancelView->busy() && !cancelView->plan().can_run() && cancelClosed == 0,
				L"toolbar cancellation stops analysis without closing the task");
			cancelView->analyze();
			drain(background);
			drain(ui);
			cancelView->start_run();
			reporter.check(installedCancel && installedCancel->enabled(),
				L"the same installed Cancel button enables for a reviewed run");
			if (installedCancel) installedCancel->invoke();
			drain(background);
			drain(ui);
			reporter.check(installedCancel && !installedCancel->enabled() && cancelClosed == 0 &&
				cancelView->actions == 0 && cancelView->plan().count(tasks::RowState::notRun) == 2,
				L"toolbar cancellation retains Not run results in the open task and disables after completion");

			view->change();
			drain(background);
			drain(ui);
			view->start_run();
			view->change();
			drain(background);
			drain(ui);
			reporter.check(!view->busy() && view->plan().count(tasks::RowState::notRun) == 2 &&
				viewStatus.find(L"Options changed") != std::wstring::npos,
				L"a late native control change cancels future writes without discarding their partial results");

			rejectWork = true;
			auto refusedView = std::make_shared<TestView>(rejectingDispatcher);
			refusedView->create(testFrame, {});
			refusedView->analyze();
			reporter.check(!refusedView->busy() && !refusedView->plan().can_run(),
				L"a task view whose worker queue rejects analysis does not stay permanently busy");
			rejectWork = false;
			refusedView->analyze();
			drain(background);
			drain(ui);
			reporter.check(!refusedView->busy() && refusedView->plan().can_run(),
				L"the same task view can recover by analyzing again after dispatch failure");

			auto reentrantView = std::make_shared<TestView>(dispatcher);
			bool changeFromStatus = true;
			TaskView::Host reentrantHost;
			reentrantHost.status = [&reentrantView, &changeFromStatus](const std::wstring& text)
			{
				if (text != L"Analyzing..." || !changeFromStatus) return;
				changeFromStatus = false;
				reentrantView->change();
			};
			reentrantView->create(testFrame, std::move(reentrantHost));
			reentrantView->analyze();
			drain(background);
			drain(ui);
			reporter.check(reentrantView->analyses == 1 && !reentrantView->busy() &&
				reentrantView->plan().can_run(),
				L"reentrant status callbacks cannot let an older Analyze clear or replace a newer request");

			auto earlyCancelView = std::make_shared<TestView>(dispatcher);
			TaskView::Host earlyCancelHost;
			earlyCancelHost.status = [&earlyCancelView](const std::wstring& text)
			{
				if (text == L"Processing...") earlyCancelView->cancel();
			};
			earlyCancelView->create(testFrame, std::move(earlyCancelHost));
			earlyCancelView->analyze();
			drain(background);
			drain(ui);
			earlyCancelView->start_run();
			reporter.check(background.empty() && !earlyCancelView->busy() && earlyCancelView->actions == 0 &&
				earlyCancelView->plan().count(tasks::RowState::notRun) == 2,
				L"Cancel from a reentrant start notification cannot be lost before the worker is submitted");

			auto explicitView = std::make_shared<TestView>(dispatcher);
			explicitView->automatic = false;
			explicitView->create(testFrame, {});
			explicitView->initialize();
			reporter.check(!explicitView->analyzes_automatically() && view->analyzes_automatically() &&
				background.empty() && explicitView->analyses == 0 && !explicitView->busy() &&
				!explicitView->plan().can_run(), L"initializing an explicit-analysis task does not scan its folder trees");

			auto refreshingView = std::make_shared<TestView>(dispatcher);
			bool preservedCompletion = false;
			TaskView::Host refreshingHost;
			refreshingHost.completed = [&refreshingView, &preservedCompletion](const tasks::TaskPlan& result)
			{
				refreshingView->change();
				preservedCompletion = result.count(tasks::RowState::success) == 2;
			};
			refreshingView->create(testFrame, std::move(refreshingHost));
			refreshingView->initialize();
			drain(background);
			drain(ui);
			refreshingView->start_run();
			drain(background);
			drain(ui);
			reporter.check(preservedCompletion && refreshingView->busy(),
				L"host refresh can start a new analysis without invalidating the completion reference or next request");
			drain(background);
			drain(ui);
			reporter.check(!refreshingView->busy() && refreshingView->plan().can_run(),
				L"a host-triggered follow-up analysis remains runnable after the preceding run completes");

			auto nativeKeyView = std::make_shared<TestView>(dispatcher);
			int escapedInputs = 0;
			TaskView::Host nativeKeyHost;
			nativeKeyHost.closed = [&escapedInputs] { ++escapedInputs; };
			nativeKeyView->create(testFrame, std::move(nativeKeyHost));
			nativeKeyView->text_options(1);
			auto textOptions = nativeKeyView->text_options(2);
			nativeKeyView->text_options(3);
			textOptions.tabbed(false);
			reporter.check(nativeKeyView->focused_control() == 3,
				L"Tab from a native text field advances from that field's actual control id");
			textOptions.tabbed(true);
			reporter.check(nativeKeyView->focused_control() == 1,
				L"Shift-Tab from a native text field reverses drawn control focus");
			textOptions.escaped();
			reporter.check(escapedInputs == 1, L"Escape inside a native text field uses the task's guarded host close");
			nativeKeyView.reset();
			textOptions.tabbed(false);
			textOptions.escaped();
			reporter.check(escapedInputs == 1,
				L"native input callbacks cannot call a task after its lifetime has ended");

			auto ownedFrame = std::make_shared<TestFrame>();
			ownedFrame->retainReactor = true;
			auto ownedView = std::make_shared<TestView>(dispatcher);
			ownedView->create(ownedFrame, {});
			const std::weak_ptr<TaskView> weakOwnedView = ownedView;
			ownedView.reset();
			reporter.check(!weakOwnedView.expired(), L"the native frame retains its task reactor while open");
			weakOwnedView.lock()->dispose();
			reporter.check(weakOwnedView.expired() && !ownedFrame->reactor && ownedFrame->closes == 1,
				L"disposing a task closes its child and breaks the shared frame/reactor ownership cycle");

			auto externallyClosed = std::make_shared<TestView>(dispatcher);
			externallyClosed->create(ownedFrame, {});
			const std::weak_ptr<TaskView> weakExternallyClosed = externallyClosed;
			externallyClosed.reset();
			auto retainedReactor = ownedFrame->reactor;
			retainedReactor->message(ownedFrame, platform::WindowMessage::destroy);
			retainedReactor.reset();
			reporter.check(weakExternallyClosed.expired() && !ownedFrame->reactor,
				L"native child destruction also releases the retained task reactor without explicit disposal");

			auto eligibleView = std::make_shared<TestView>(dispatcher);
			eligibleView->analysisProblem = L"Choose both folders.";
			std::wstring eligibilityStatus;
			TaskView::Host eligibilityHost;
			eligibilityHost.status = [&eligibilityStatus](std::wstring text) { eligibilityStatus = std::move(text); };
			eligibleView->create(testFrame, std::move(eligibilityHost));
			const auto eligibilityToolbar = eligibleView->toolbar_items();
			const auto analysisCommand = std::ranges::find_if(eligibilityToolbar, [](const auto& item)
				{ return item.command && item.command->name == L"Refresh"; });
			reporter.check(analysisCommand != eligibilityToolbar.end() &&
				!analysisCommand->command->enabled() && analysisCommand->command->tooltip == L"Choose both folders.",
				L"analysis eligibility disables its toolbar command with the same actionable reason");
			eligibleView->analyze();
			reporter.check(background.empty() && eligibleView->analyses == 0 && !eligibleView->busy() &&
				eligibleView->plan().blockReason == L"Choose both folders.",
				L"direct Analyze cannot bypass workflow eligibility or leave a previously runnable plan");
			eligibleView->analysisProblem.clear();
			eligibleView->customSummary = L"2 copies, 0 deletions, 0 ignored";
			eligibleView->analyze();
			drain(background);
			drain(ui);
			reporter.check(eligibleView->plan().can_run() && eligibilityStatus == eligibleView->customSummary,
				L"completed analysis presents the workflow-specific action summary");

			auto rebuiltView = std::make_shared<TestView>(dispatcher);
			ui::Control rebuiltControl;
			rebuiltControl.id = 42;
			rebuiltControl.kind = ui::ControlKind::checkBox;
			rebuiltView->controlsToBuild.push_back(rebuiltControl);
			rebuiltView->create(testFrame, {});
			rebuiltView->focus_control(42);
			rebuiltView->rebuild_for_test();
			reporter.check(rebuiltView->focused_control() == 42,
				L"workflow control rebuilds preserve keyboard focus on the same still-enabled control");
			rebuiltView->controlsToBuild.front().enabled = false;
			rebuiltView->rebuild_for_test();
			reporter.check(rebuiltView->focused_control() == 0,
				L"rebuilding a disabled control does not leave stale keyboard focus");
		}

		void test_file_services(Reporter& reporter)
		{
			reporter.section(L"files sidecars, unique names, and transfers");
			std::error_code error;
			const auto root = platform::unique_temp_folder(L"iw30-task-tests");
			std::filesystem::remove_all(root, error);
			std::filesystem::create_directories(root, error);
			const auto write = [&root](const std::wstring& name, const std::string& content)
			{
				std::ofstream stream(root / name, std::ios::binary);
				stream.write(content.data(), static_cast<std::streamsize>(content.size()));
			};

			write(L"photo.jpg", "primary");
			write(L"photo.xmp", "sidecar");
			write(L"photo.jpg.thm", "thumb");
			write(L"other.xmp", "unrelated");
			const auto sidecars = files::sidecar_paths(root / L"photo.jpg");
			reporter.check(sidecars.size() == 2, L"sidecar_paths finds only companions that exist");
			reporter.check(std::ranges::any_of(sidecars, [](const std::filesystem::path& path)
			               {
				               return path.filename() == L"photo.xmp";
			               }) && std::ranges::any_of(sidecars, [](const std::filesystem::path& path)
			               {
				               return path.filename() == L"photo.jpg.thm";
			               }),
			               L"both the stem and the full-name sidecar conventions are recognised");
			reporter.check(files::sidecar_paths(root / L"missing.jpg").empty(),
			               L"a primary with no companions has no sidecars");

			reporter.check(files::unique_destination(root / L"free.jpg") == root / L"free.jpg",
			               L"unique_destination returns a free proposal unchanged");
			const auto renamed = files::unique_destination(root / L"photo.jpg");
			reporter.check(renamed.filename() == L"photo (2).jpg" && !std::filesystem::exists(renamed, error),
			               L"unique_destination never returns an existing path");
			write(L"photo (2).jpg", "taken");
			reporter.check(files::unique_destination(root / L"photo.jpg").filename() == L"photo (3).jpg",
			               L"unique_destination keeps counting past occupied names");
			reporter.check(files::unique_destination(root / L"free.jpg", [](const std::filesystem::path&)
			               {
				               return true;
			               }).empty(),
			               L"unique_destination is bounded and gives up rather than looping");

			reporter.check(!files::copy_file_to(root / L"photo.jpg", root / L"sub" / L"copied.jpg", false),
			               L"copy creates a missing destination folder");
			reporter.check(std::filesystem::exists(root / L"sub" / L"copied.jpg", error) &&
			               std::filesystem::exists(root / L"photo.jpg", error),
			               L"copy keeps every source file");
			reporter.check(files::copy_file_to(root / L"photo.jpg", root / L"sub" / L"copied.jpg", false) ==
			               std::errc::file_exists,
			               L"copy refuses an existing destination unless overwrite was asked for");
			reporter.check(!files::copy_file_to(root / L"photo.jpg", root / L"sub" / L"copied.jpg", true),
			               L"copy replaces an existing destination when overwrite was asked for");
			reporter.check(!files::move_file_to(root / L"sub" / L"copied.jpg", root / L"sub" / L"moved.jpg", false) &&
			               !std::filesystem::exists(root / L"sub" / L"copied.jpg", error),
			               L"move removes the source after a successful move");
			reporter.check(files::move_file_to(root / L"photo.jpg", root / L"sub" / L"moved.jpg", false) ==
			               std::errc::file_exists,
			               L"move refuses an existing destination unless overwrite was asked for");
			reporter.check(std::filesystem::exists(root / L"photo.jpg", error),
			               L"a refused move leaves the source where it was");
			const auto read = [](const std::filesystem::path& path)
			{
				std::ifstream stream(path, std::ios::binary);
				return std::string(std::istreambuf_iterator<char>(stream), {});
			};
			write(L"replace.txt", "unchanged");
			reporter.check(files::copy_file_to(root / L"missing.txt", root / L"replace.txt", true) &&
				read(root / L"replace.txt") == "unchanged",
				L"a failed replacement copy leaves the existing destination intact");
			reporter.check(files::move_file_to(root / L"missing.txt", root / L"replace.txt", true) &&
				read(root / L"replace.txt") == "unchanged",
				L"a failed replacement move leaves the existing destination intact");
			reporter.check(files::copy_file_to(root / L"photo.jpg", root / L"photo.jpg", true) &&
				read(root / L"photo.jpg") == "primary", L"copy cannot replace its own source");
			std::filesystem::create_directory(root / L"folder", error);
			std::ofstream(root / L"folder" / L"child.txt") << "child";
			reporter.check(!files::move_file_to(root / L"folder", root / L"renamed-folder", false) &&
				read(root / L"renamed-folder" / L"child.txt") == "child",
				L"directory renames retain their complete contents");
			reporter.check(std::ranges::none_of(std::filesystem::directory_iterator(root), [](const auto& entry)
			{
				return entry.path().filename().wstring().starts_with(L".iw30-");
			}), L"failed transfers remove their incomplete temporary output");

			std::filesystem::remove_all(root, error);
		}

		void test_rename_tokens(Reporter& reporter)
		{
			reporter.section(L"files::expand_tokens");
			files::Metadata metadata;
			metadata.dateTaken = L"2019:07:04 21:05:09";
			metadata.camera = L"Canon EOS";
			const std::filesystem::path path = L"C:\\trip\\Italy\\DSC_0042.JPG";

			reporter.check(files::expand_tokens(L"file-###", metadata, path, 7) == L"file-007",
			               L"a run of # is the zero-padded sequence at its own width");
			reporter.check(files::expand_tokens(L"#", metadata, path, 12) == L"12",
			               L"a sequence wider than its field is not truncated");
			reporter.check(files::expand_tokens(L"{created}-##", metadata, path, 3) == L"2019-07-04-03",
			               L"{created} expands the capture date");
			reporter.check(files::expand_tokens(L"{year}-{month}-###", metadata, path, 1) == L"2019-07-001",
			               L"{year} and {month} expand independently");
			reporter.check(files::expand_tokens(L"{name}", metadata, path, 1) == L"DSC_0042",
			               L"{name} is the original name without its extension");
			reporter.check(files::expand_tokens(L"{folder}-{camera}", metadata, path, 1) == L"Italy-Canon EOS",
			               L"folder and metadata tokens expand");
			reporter.check(files::expand_tokens(L"{unknown}x", metadata, path, 1) == L"x",
			               L"an unknown token expands to nothing rather than to itself");
			reporter.check(files::expand_tokens(L"a{b", metadata, path, 1) == L"a{b",
			               L"an unterminated token is literal text");
			files::Metadata undated;
			reporter.check(files::expand_tokens(L"{year}", undated, path, 1).empty(),
			               L"a date token with no known date expands to nothing");
		}

		void test_convert_sizing(Reporter& reporter)
		{
			reporter.section(L"Convert maximum dimension");
			reporter.check(limited_output_size(4000, 3000, 0).width == 4000,
			               L"no limit leaves the photo at its own size");
			reporter.check(limited_output_size(800, 600, 1920).width == 800 &&
			               limited_output_size(800, 600, 1920).height == 600,
			               L"a photo already inside the limit is never enlarged");
			const auto reduced = limited_output_size(4000, 3000, 1000);
			reporter.check(reduced.width == 1000 && reduced.height == 750,
			               L"the longest side meets the limit and the aspect ratio is preserved");
			const auto portrait = limited_output_size(3000, 4000, 1000);
			reporter.check(portrait.width == 750 && portrait.height == 1000,
			               L"the limit applies to whichever side is longest");
			const auto extreme = limited_output_size(10000, 3, 100);
			reporter.check(extreme.width == 100 && extreme.height == 1,
			               L"an extreme aspect ratio still yields at least one pixel");

			reporter.section(L"Convert reviewed execution");
			const auto root = files::unique_destination(std::filesystem::current_path() / L"iw30-convert-tests");
			std::error_code error;
			const bool owned = !root.empty() && std::filesystem::create_directory(root, error);
			reporter.check(owned && !error, L"Convert tests create an isolated owned folder");
			if (!owned || error) return;
			struct Cleanup
			{
				std::filesystem::path path;
				~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
			} cleanup{root};
			const auto write = [](const std::filesystem::path& path, const std::string& bytes)
			{
				std::ofstream file(path, std::ios::binary | std::ios::trunc);
				file << bytes;
			};
			const auto read = [](const std::filesystem::path& path)
			{
				std::ifstream file(path, std::ios::binary);
				return std::string(std::istreambuf_iterator<char>(file), {});
			};
			files::DecodedImage image;
			image.width = 4;
			image.height = 2;
			image.pixels.assign(8, 0xff336699u);
			const auto source = root / L"photo.png";
			const auto second = root / L"second.png";
			reporter.check(files::save_image(image, source, files::ImageSaveFormat::png, {}, false) &&
				files::save_image(image, second, files::ImageSaveFormat::png, {}, false),
				L"Convert fixtures use a real decodable image");
			const auto original = read(source);
			convert::Settings settings;
			settings.format = files::ImageSaveFormat::png;
			reporter.check(!convert::analyze({source}, settings).can_run(), L"a missing destination blocks conversion");
			settings.destination = root / L"output";
			settings.limitEnabled = true;
			settings.maximumDimension = 0;
			reporter.check(!convert::settings_problem(settings).empty(), L"an enabled zero dimension has a Run blocker");
			settings.maximumDimension = 2;
			auto reviewed = convert::analyze({source}, settings);
			const auto originalChange = reviewed.rows.front().change;
			const auto generation = reviewed.generation;
			const auto preview = convert::describe_changes(reviewed.rows.front(), settings);
			settings.maximumDimension = 1;
			settings.quality = 42;
			reporter.check(preview.find(L"2 x 1") != std::wstring::npos &&
				convert::describe_changes(reviewed.rows.front(), settings).find(L"1 x 1") != std::wstring::npos &&
				reviewed.generation == generation && reviewed.rows.front().change == originalChange,
				L"dimension and quality previews preserve the immutable filesystem plan and its generation");

			settings.destination = root;
			settings.policy = tasks::CollisionPolicy::replace;
			reporter.check(!convert::analyze({source}, settings).can_run(),
				L"Replace cannot overwrite the original photo in its own folder");
			settings.policy = tasks::CollisionPolicy::skip;
			reporter.check(!convert::analyze({source}, settings).can_run(), L"an all-skipped conversion cannot run");
			settings.policy = tasks::CollisionPolicy::autoRename;
			auto autoPlan = convert::analyze({source}, settings);
			reporter.check(autoPlan.can_run() && !paths::equal(source, autoPlan.rows.front().destination),
				L"Auto-rename retains originals while converting into their own folder");

			settings.destination = root / L"output";
			settings.policy = tasks::CollisionPolicy::block;
			auto appeared = convert::analyze({source}, settings);
			std::filesystem::create_directory(settings.destination, error);
			write(appeared.rows.front().destination, "arrived after Review");
			tasks::run_plan(appeared, {}, convert::run_options(settings, {source}));
			reporter.check(appeared.rows.front().state == tasks::RowState::skipped &&
				read(appeared.rows.front().destination) == "arrived after Review" && read(source) == original,
				L"an unreviewed arrival is skipped and neither destination nor original is changed");

			settings.policy = tasks::CollisionPolicy::replace;
			auto replacement = convert::analyze({source}, settings);
			reporter.check(replacement.can_run() && replacement.rows.front().replace,
				L"the existing destination is explicitly reviewed as Replace");
			tasks::run_plan(replacement, {}, convert::run_options(settings, {source}));
			const auto converted = files::load_image(replacement.rows.front().destination);
			reporter.check(replacement.count(tasks::RowState::success) == 1 && converted.width == 1 &&
				converted.height == 1 && read(source) == original,
				L"approved Replace writes the reviewed transformation and retains the original bytes");
			auto changedReplacement = convert::analyze({source}, settings);
			write(changedReplacement.rows.front().destination, "a different destination appeared");
			tasks::run_plan(changedReplacement, {}, convert::run_options(settings, {source}));
			reporter.check(changedReplacement.count(tasks::RowState::failed) == 1 &&
				read(changedReplacement.rows.front().destination) == "a different destination appeared",
				L"Replace refuses a destination whose reviewed identity or contents changed");

			settings.destination = root / L"cancel";
			settings.policy = tasks::CollisionPolicy::block;
			auto cancelled = convert::analyze({source, second}, settings);
			std::stop_source stop;
			auto options = convert::run_options(settings, {source, second});
			options.progress = [&stop](const size_t complete, size_t) { if (complete == 1) stop.request_stop(); };
			tasks::run_plan(cancelled, stop.get_token(), options);
			reporter.check(cancelled.count(tasks::RowState::success) == 1 && cancelled.count(tasks::RowState::notRun) == 1 &&
				std::filesystem::exists(cancelled.rows.front().destination) &&
				!std::filesystem::exists(cancelled.rows.back().destination),
				L"cancellation retains completed conversion output and marks unreached items Not run");

			const auto malformed = root / L"malformed.png";
			write(malformed, "not an image");
			settings.destination = root / L"continue";
			auto continued = convert::analyze({malformed, second}, settings);
			tasks::run_plan(continued, {}, convert::run_options(settings, {malformed, second}));
			reporter.check(continued.rows[0].state == tasks::RowState::failed &&
				continued.rows[1].state == tasks::RowState::success,
				L"a decoding failure does not prevent a later conversion");
			settings.destination = root / L"changed-source";
			auto stalePhoto = convert::analyze({source}, settings);
			write(source, "a replacement photo");
			tasks::run_plan(stalePhoto, {}, convert::run_options(settings, {source}));
			reporter.check(stalePhoto.count(tasks::RowState::failed) == 1 &&
				!std::filesystem::exists(stalePhoto.rows.front().destination),
				L"conversion refuses a source changed since its dimensions were reviewed");
			const auto missingSource = root / L"missing-source.png";
			reporter.check(files::save_image(image, missingSource, files::ImageSaveFormat::png, {}, false),
				L"the missing-source fixture is created");
			settings.destination = root / L"missing-source-output";
			auto missingSourcePlan = convert::analyze({missingSource}, settings);
			std::filesystem::remove(missingSource, error);
			tasks::run_plan(missingSourcePlan, {}, convert::run_options(settings, {missingSource}));
			reporter.check(missingSourcePlan.count(tasks::RowState::failed) == 1 &&
				missingSourcePlan.rows.front().detail.find(L"Analyze again") != std::wstring::npos,
				L"a source removed after Review fails with an actionable explanation");
		}

		void test_tools_configuration(Reporter& reporter)
		{
			reporter.section(L"tools configuration");
			constexpr std::wstring_view document =
				LR"({"tools":{"folders":["C:\\Apps"],"apps":[)"
				LR"({"exe":"paint.exe","invoke":"{exe-path} {item-path}","extensions":"jpg,PNG","text":"Paint"},)"
				LR"({"invoke":"{exe-path}","text":"No executable"},)"
				LR"({"exe":"only.exe","text":"No template"}]}})";
			const auto configuration = tools::parse_configuration(document);
			reporter.check(configuration.valid, L"a well-formed document is accepted");
			reporter.check(configuration.folders.size() == 1, L"configured search roots are read");
			reporter.check(configuration.apps.size() == 1,
			               L"a declaration without exe or invoke is rejected outright");
			reporter.check(configuration.issues.size() == 2,
				L"each unusable declaration produces a user-visible configuration issue");
			reporter.check(!tools::declaration_is_usable({L"", L"{exe-path}"}) &&
			               !tools::declaration_is_usable({L"a.exe", L""}) &&
			               tools::declaration_is_usable({L"a.exe", L"{exe-path}"}),
			               L"both exe and invoke are required");

			const auto extensions = tools::split_extensions(L"jpg, PNG;.tif");
			reporter.check(extensions.size() == 3 && extensions[0] == L".jpg" && extensions[1] == L".png" &&
			               extensions[2] == L".tif",
			               L"extensions are normalised to lowercase with a leading dot");
			reporter.check(tools::split_extensions(L"jpg,JPG").size() == 1,
			               L"an extension repeated in one declaration is listed once");

			bool hasGroup = false;
			reporter.check(tools::parse_group(L"photo", hasGroup) == files::ItemKind::image && hasGroup,
			               L"the photo media group is recognised");
			tools::parse_group(L"nonsense", hasGroup);
			reporter.check(!hasGroup, L"an unknown media group leaves the tool without one");

			std::wstring nested(64, L'[');
			reporter.check(!tools::parse_configuration(L"{\"tools\":{\"apps\":" + nested).valid,
			               L"a document nested past the limit is rejected without throwing");
			reporter.check(!tools::parse_configuration(L"not json at all").valid,
			               L"a document that is not JSON is rejected");
			reporter.check(!tools::parse_configuration(L"").valid, L"an empty document is rejected");

			std::vector<tools::InstalledTool> installed;
			installed.push_back({L"Group tool", L"C:\\Apps\\group.exe", L"{exe-path} {item-path}", {},
			                     files::ItemKind::image, true});
			installed.push_back({L"Both", L"C:\\Apps\\both.exe", L"{exe-path} {item-path}", {L".jpg"},
			                     files::ItemKind::image, true});
			installed.push_back({L"Extension tool", L"C:\\Apps\\ext.exe", L"{exe-path} {item-path}", {L".JPG"}});
			const auto matched = tools::tools_for(installed, L"C:\\photos\\a.jpg");
			reporter.check(matched.size() == 3, L"extension and media-group tools are both offered");
			reporter.check(matched[0]->name == L"Both" && matched[1]->name == L"Extension tool" &&
			               matched[2]->name == L"Group tool",
			               L"extension tools sort before group tools");
			reporter.check(std::ranges::count(matched, installed.data() + 1) == 1,
			               L"a tool matching by extension and by group is listed once");
			reporter.check(tools::tools_for(installed, L"C:\\photos\\a.txt").empty(),
			               L"an unsupported extension offers no tool");

			const tools::InstalledTool spaced{
				L"Spaced", L"C:\\Program Files\\App\\run.exe", L"{exe-path} --open {item-path}"
			};
			const auto arguments = tools::build_arguments(spaced, L"C:\\my photos\\a b.jpg");
			reporter.check(arguments == LR"(--open "C:\my photos\a b.jpg")",
			               L"the template's own program token is dropped and paths with spaces are quoted");
			const tools::InstalledTool plain{L"Plain", L"C:\\Apps\\run.exe", L"{exe-path} {item-path}"};
			reporter.check(tools::build_arguments(plain, L"C:\\a.jpg") == L"\"C:\\a.jpg\"",
			               L"an item path is always quoted, including one without spaces");
			const tools::InstalledTool alreadyQuoted{L"Quoted", L"C:\\Apps\\run.exe",
				L"  \"{exe-path}\" --open \"{item-path}\""};
			reporter.check(tools::build_arguments(alreadyQuoted, L"C:\\my photos\\a.jpg") ==
				L"--open \"C:\\my photos\\a.jpg\"", L"templates which quote placeholders do not double-quote paths");
			reporter.check(tools::build_arguments(plain, L"C:\\photos\\") == L"\"C:\\photos\\\\\"",
				L"a trailing backslash is escaped before the closing command-line quote");
			const std::array<std::filesystem::path, 2> multiple{
				L"C:\\my photos\\first.jpg", L"C:\\my photos\\second \u00e9.jpg"};
			const tools::InstalledTool multi{L"Multi", L"C:\\Apps\\run.exe",
				L"{exe-path} --open {item-paths}"};
			reporter.check(tools::accepts_multiple_items(multi) &&
				tools::build_arguments(multi, multiple) ==
					L"--open \"C:\\my photos\\first.jpg\" \"C:\\my photos\\second \u00e9.jpg\"",
				L"an opted-in tool receives every selected path, quoted independently");
			const tools::InstalledTool multiQuoted{L"Multi", L"C:\\Apps\\run.exe",
				L"{exe-path} --open \"{item-paths}\""};
			reporter.check(tools::build_arguments(multiQuoted, multiple) == tools::build_arguments(multi, multiple),
				L"quoting the multi-item placeholder does not combine all paths into one argument");
			reporter.check(!tools::accepts_multiple_items(plain) && tools::build_arguments(plain, multiple).empty(),
				L"a single-file declaration cannot silently receive a multi-file invocation");
			const std::vector<tools::InstalledTool> mixedTools{
				{L"Photo batch", L"C:\\Apps\\photo.exe", L"{exe-path} {item-paths}", {L".jpg"}, files::ItemKind::image, true},
				{L"JPEG batch", L"C:\\Apps\\jpeg.exe", L"{exe-path} {item-paths}", {L".jpg"}},
				{L"Legacy", L"C:\\Apps\\legacy.exe", L"{exe-path} {item-path}", {L".jpg"}}
			};
			const std::array<std::filesystem::path, 2> mixedFiles{L"C:\\a.jpg", L"C:\\b.png"};
			const auto mixedMatches = tools::tools_for(mixedTools, mixedFiles);
			reporter.check(mixedMatches.size() == 2 && mixedMatches[0]->name == L"Photo batch" &&
				mixedMatches[1]->name == L"Legacy",
				L"mixed file types require a batch tool to accept every item while legacy tools retain first-item behavior");
			const std::vector<tools::InstalledTool> aliases{
				{L"JPEG alias", L"C:\\Apps\\photo.exe", L"{exe-path} {item-paths}", {L".jpg"}},
				{L"PNG alias", L"C:\\Apps\\photo.exe", L"{exe-path} {item-paths}", {L".png"}}
			};
			reporter.check(tools::tools_for(aliases, mixedFiles).size() == 1,
				L"declarations of the same executable and template combine their accepted file types without duplicate entries");
			reporter.check(!tools::declaration_is_usable({L"C:\\Apps\\run.exe", L"{exe-path} {item-path}"}) &&
				!tools::declaration_is_usable({L"run.exe", L"other.exe {item-path}"}) &&
				!tools::declaration_is_usable({L"run.exe", L"{exe-path}suffix {item-path}"}) &&
				!tools::declaration_is_usable({L"run.cmd", L"{exe-path} {item-path}"}) &&
				tools::declaration_is_usable({L"run", L"{exe-path} {item-path}"}),
				L"tools require discovered executable base names rather than paths or arbitrary commands");
			const std::wstring_view invalidDocuments[]{
				LR"({"tools":{"folders":["C:\\Apps"},"apps":[]}})",
				LR"({"tools":{}} trailing)",
				LR"({"tools":{"folders":false}})",
				LR"({"tools":{},"unknown":banana})",
				LR"({"tools":{},"unknown":01})",
				LR"({"tools":{},"unknown":1.})",
				LR"({"tools":{},"unknown":1e+})",
				LR"({"tools":{},"unknown":"\q"})",
				LR"({"tools":{},"unknown":"\u12"})",
				LR"({"tools":{},"unknown":"\ud800"})",
				LR"({"tools":{},"unknown":"\udc00"})",
				L"{\"tools\":{},\"unknown\":\"raw\nnewline\"}",
				LR"({"tools":{},"unknown":[1,]})"
			};
			for (const auto invalid : invalidDocuments)
				reporter.check(!tools::parse_configuration(invalid).valid, L"malformed JSON is rejected completely");
			reporter.check(tools::parse_configuration(
				LR"({"tools":{},"maps":{"ignored":[-0.5e+2,true,false,null,"\ud83d\ude00"]}})").valid,
				L"empty tools and well-formed ignored JSON values, including Unicode pairs, are accepted");
			const auto wrongFieldType = tools::parse_configuration(
				LR"({"tools":{"apps":[{"exe":12,"invoke":"{exe-path}"},{"exe":"ok.exe","invoke":"{exe-path}"}]}})");
			reporter.check(wrongFieldType.valid && wrongFieldType.apps.size() == 1,
				L"a malformed app declaration is omitted without hiding the other valid declarations");
			const auto deep = L"{\"tools\":{},\"unknown\":" + std::wstring(32, L'[') +
				L"null" + std::wstring(32, L']') + L"}";
			reporter.check(!tools::parse_configuration(deep).valid, L"ignored values obey the JSON depth bound");
			reporter.check(!tools::parse_configuration(std::wstring(256 * 1024 + 1, L' ')).valid,
				L"the in-memory JSON reader enforces its document size bound");
			installed.push_back(installed[1]);
			reporter.check(tools::tools_for(installed, L"C:\\photos\\a.jpg").size() == 3,
				L"duplicate declarations of the same executable and command are listed once");

			const auto root = std::filesystem::current_path() /
				(L"tools-test-fixture-" + std::to_wstring(std::filesystem::file_time_type::clock::now().time_since_epoch().count()));
			std::error_code error;
			const bool created = std::filesystem::create_directory(root, error);
			reporter.check(created && !error, L"an isolated project-local discovery fixture can be created");
			if (created && !error)
			{
				struct Cleanup
				{
					std::filesystem::path path;
					~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
				} cleanup{root};
				reporter.section(L"latest-run UTF-8 diagnostic log");
				diagnostics::LogFile log;
				reporter.check(log.write(L"not open") == std::errc::bad_file_descriptor,
					L"logging to a closed file reports failure");
				const auto logPath = root / L"run.log";
				reporter.check(!log.open(logPath), L"a diagnostic log opens in an owned fixture");
				diagnostics::LogFile competingLog;
				reporter.check(static_cast<bool>(competingLog.open(logPath)),
					L"a second process or writer cannot truncate a live run's log");
				reporter.check(!log.write(L"Unicode \u00e9 \u4e2d"), L"Unicode diagnostics are written");
				std::atomic_bool writesSucceeded{true};
				const auto writer = [&log, &writesSucceeded]
				{
					for (int i = 0; i < 50; ++i)
						if (log.write(L"thread diagnostic")) writesSucceeded = false;
				};
				std::thread first(writer), second(writer);
				first.join();
				second.join();
				log.close();
				std::ifstream logInput(logPath, std::ios::binary);
				const std::string logBytes((std::istreambuf_iterator<char>(logInput)), {});
				logInput.close();
				reporter.check(writesSucceeded && std::ranges::count(logBytes, '\n') == 101 &&
					logBytes.find("Unicode \xc3\xa9 \xe4\xb8\xad") != std::string::npos,
					L"threaded UTF-8 log writes retain every complete record");
				reporter.check(!log.open(logPath) && !log.write(L"latest run"),
					L"reopening starts a new run");
				log.close();
				std::ifstream latest(logPath, std::ios::binary);
				const std::string latestBytes((std::istreambuf_iterator<char>(latest)), {});
				latest.close();
				reporter.check(latestBytes.find("latest run") != std::string::npos &&
					latestBytes.find("Unicode") == std::string::npos,
					L"the latest-run log does not retain stale content");
				reporter.check(static_cast<bool>(log.open(root / L"missing" / L"run.log")),
					L"an unavailable log destination reports failure");
				reporter.section(L"tools discovery and configuration diagnostics");
				const auto fourth = root / L"one" / L"two" / L"three" / L"four";
				std::filesystem::create_directories(fourth / L"five", error);
				std::ofstream(fourth / L"PaInT.EXE") << "fixture";
				std::ofstream(fourth / L"five" / L"hidden.exe") << "fixture";
				tools::Configuration discovery;
				discovery.valid = true;
				discovery.folders.push_back(root);
				discovery.apps = {
					{L"paint", L"{exe-path} {item-path}", L"jpg"},
					{L"hidden.exe", L"{exe-path} {item-path}", L"jpg"},
					{L"missing.exe", L"{exe-path} {item-path}", L"jpg"},
					{L"paint.exe", L"undiscovered.exe {item-path}", L"jpg"}};
				const auto found = tools::discover(discovery);
				reporter.check(found.size() == 1 && paths::iequals(found.front().executable.filename().wstring(), L"paint.exe"),
					L"discovery includes depth four, ignores depth five, and matches executable base names case-insensitively");
				reporter.check(tools::discover(discovery, 0).empty(), L"zero search depth examines only the configured root");

				std::ofstream(root / L"paint.exe") << "fixture";
				std::string escapedRoot;
				for (const auto character : root.u8string())
				{
					if (character == u8'\\') escapedRoot += '\\';
					escapedRoot += static_cast<char>(character);
				}
				const auto configurationPath = root / L"tools.json";
				{
					std::ofstream stream(configurationPath, std::ios::binary);
					stream << "\xef\xbb\xbf{\"tools\":{\"folders\":[\"" << escapedRoot <<
						"\"],\"apps\":[{\"exe\":\"paint.exe\",\"invoke\":\"{exe-path} {item-path}\","
						"\"extensions\":\"jpg\",\"text\":\"\xc3\x89" "diteur\"}]}}";
				}
				tools::ToolTable table;
				table.load(configurationPath);
				reporter.check(table.installed().size() == 1 && table.installed().front().name == L"\u00c9diteur",
					L"UTF-8 tool names and a UTF-8 BOM are decoded losslessly");
				{
					std::ofstream stream(configurationPath, std::ios::binary);
					stream << "{\"tools\":{},\"text\":\"\xc0\xaf\"}";
				}
				table.load(configurationPath);
				reporter.check(table.installed().empty(), L"invalid UTF-8 clears the table instead of guessing Latin-1");
				reporter.check(!table.issues().empty(), L"invalid tool files retain a visible diagnostic");
				table.load(root / L"missing-tools.json");
				reporter.check(table.installed().empty() && !table.issues().empty() &&
					table.issue_summary().find(L"missing-tools.json") != std::wstring::npos,
					L"a missing tool file identifies the configuration path rather than failing silently");
				const auto reviewedPath = root / L"reviewed.jpg";
				const auto replacementPath = root / L"replacement.jpg";
				std::ofstream(reviewedPath) << "same data";
				std::ofstream(replacementPath) << "same data";
				const auto modified = std::filesystem::last_write_time(reviewedPath, error);
				if (!error) std::filesystem::last_write_time(replacementPath, modified, error);
				const auto reviewed = files::snapshot_file(reviewedPath);
				reporter.check(!error && reviewed && files::matches_snapshot(reviewedPath, *reviewed),
					L"a transfer destination snapshot recognizes the unchanged reviewed file");
				reporter.check(reviewed && !files::matches_snapshot(replacementPath, *reviewed),
					L"file identity distinguishes different destinations with identical data and timestamps");
				std::filesystem::remove(reviewedPath, error);
				if (!error) std::filesystem::rename(replacementPath, reviewedPath, error);
				reporter.check(!error && reviewed && !files::matches_snapshot(reviewedPath, *reviewed),
					L"an externally replaced transfer destination no longer matches Review");
				const auto absentPath = root / L"not-reviewed.jpg";
				const auto absent = files::snapshot_file(absentPath);
				std::ofstream(absentPath) << "created after review";
				reporter.check(absent && !absent->exists && !files::matches_snapshot(absentPath, *absent),
					L"a destination created after Review is not granted overwrite permission");
				const auto transferSource = root / L"transfer-source.jpg";
				std::ofstream(transferSource) << "source bytes";
				const auto commitReview = files::snapshot_file(reviewedPath);
				const auto refusedCopy = files::copy_file_to(transferSource, reviewedPath, true, [&]
				{
					std::ofstream(reviewedPath) << "changed while copying";
					return commitReview && files::matches_snapshot(reviewedPath, *commitReview);
				});
				std::string preservedDestination;
				{
					std::ifstream stream(reviewedPath);
					std::getline(stream, preservedDestination);
				}
				reporter.check(refusedCopy == std::errc::operation_canceled &&
					preservedDestination == "changed while copying",
					L"a destination changed during copying survives a refused final commit");
				const auto moveTarget = root / L"move-target.jpg";
				const auto refusedMove = files::move_file_to(transferSource, moveTarget, false, [] { return false; });
				reporter.check(refusedMove == std::errc::operation_canceled &&
					std::filesystem::exists(transferSource) && !std::filesystem::exists(moveTarget),
					L"a refused move commit neither removes the source nor creates an unreviewed target");
			}
		}

		void test_rename_staging(Reporter& reporter)
		{
			reporter.section(L"rename staging");
			reporter.check(rename::sidecar_destination(L"C:\\a\\photo.xmp", L"C:\\a\\photo.jpg",
			                                           L"C:\\a\\new.jpg") == L"C:\\a\\new.xmp",
			               L"a stem sidecar follows the primary's new stem");
			reporter.check(rename::sidecar_destination(L"C:\\a\\photo.jpg.thm", L"C:\\a\\photo.jpg",
			                                           L"C:\\a\\new.jpg") == L"C:\\a\\new.jpg.thm",
			               L"a full-name sidecar keeps the primary's whole new name");

			std::error_code error;
			const auto root = files::unique_destination(std::filesystem::current_path() / L"iw30-rename-tests");
			const bool owned = !root.empty() && std::filesystem::create_directory(root, error);
			reporter.check(owned && !error, L"Rename tests create an isolated owned folder");
			if (!owned || error) return;
			struct Cleanup
			{
				std::filesystem::path path;
				~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
			} cleanup{root};
			const auto write = [&root](const std::wstring& name, const std::string& content)
			{
				std::ofstream stream(root / name, std::ios::binary);
				stream.write(content.data(), static_cast<std::streamsize>(content.size()));
			};
			const auto content = [&root](const std::wstring& name)
			{
				std::ifstream stream(root / name, std::ios::binary);
				std::string text;
				std::getline(stream, text);
				return text;
			};
			const auto row = [](const std::filesystem::path& source, const std::filesystem::path& destination)
			{
				tasks::TaskRow value;
				value.source = source;
				value.destination = destination;
				value.state = tasks::RowState::ready;
				return value;
			};

			// A two-file name swap: neither file may be destroyed by the other taking its name.
			write(L"a.txt", "first");
			write(L"b.txt", "second");
			{
				tasks::TaskPlan plan;
				plan.rows.push_back(row(root / L"a.txt", root / L"b.txt"));
				plan.rows.push_back(row(root / L"b.txt", root / L"a.txt"));
				rename::Staging staging;
				const auto planned = [&plan](const std::filesystem::path& path)
				{
					return std::ranges::any_of(plan.rows, [&path](const tasks::TaskRow& value)
					{
						return paths::equal(value.source, path);
					});
				};
				const std::stop_source running;
				tasks::run_plan(plan, running.get_token(), {
					                {}, [&staging, &planned](tasks::TaskRow& value)
					                {
						                rename::rename_row(value, staging, planned);
					                },
					                {}
				                });
				reporter.check(plan.count(tasks::RowState::success) == 2,
				               L"a two-file name swap completes");
				reporter.check(staging.pending() == 0, L"nothing is left under a temporary name");
			}
			reporter.check(content(L"a.txt") == "second" && content(L"b.txt") == "first",
			               L"a swap exchanges the two files rather than destroying one");

			// A three-file cycle: a -> b -> c -> a.
			write(L"c1.txt", "one");
			write(L"c2.txt", "two");
			write(L"c3.txt", "three");
			{
				tasks::TaskPlan plan;
				plan.rows.push_back(row(root / L"c1.txt", root / L"c2.txt"));
				plan.rows.push_back(row(root / L"c2.txt", root / L"c3.txt"));
				plan.rows.push_back(row(root / L"c3.txt", root / L"c1.txt"));
				rename::Staging staging;
				const auto planned = [&plan](const std::filesystem::path& path)
				{
					return std::ranges::any_of(plan.rows, [&path](const tasks::TaskRow& value)
					{
						return paths::equal(value.source, path);
					});
				};
				const std::stop_source running;
				tasks::run_plan(plan, running.get_token(), {
					                {}, [&staging, &planned](tasks::TaskRow& value)
					                {
						                rename::rename_row(value, staging, planned);
					                },
					                {}
				                });
				reporter.check(plan.count(tasks::RowState::success) == 3, L"a three-file cycle completes");
			}
			reporter.check(content(L"c2.txt") == "one" && content(L"c3.txt") == "two" &&
			               content(L"c1.txt") == "three",
			               L"every file in a cycle ends up with the right contents");

			// Sidecars move as one logical rename with their primary.
			write(L"photo.jpg", "pixels");
			write(L"photo.xmp", "metadata");
			write(L"photo.jpg.thm", "thumb");
			{
				auto value = row(root / L"photo.jpg", root / L"holiday.jpg");
				value.sidecars = files::sidecar_paths(root / L"photo.jpg");
				rename::Staging staging;
				rename::rename_row(value, staging, {});
				reporter.check(value.sidecars.size() == 2, L"both companion conventions are collected");
			}
			reporter.check(std::filesystem::exists(root / L"holiday.jpg", error) &&
			               std::filesystem::exists(root / L"holiday.xmp", error) &&
			               std::filesystem::exists(root / L"holiday.jpg.thm", error) &&
			               !std::filesystem::exists(root / L"photo.jpg", error),
			               L"sidecars are renamed with their primary");

			// A destination that appeared since Review and is not one of ours is skipped.
			write(L"src.txt", "source");
			write(L"taken.txt", "existing");
			{
				auto value = row(root / L"src.txt", root / L"taken.txt");
				rename::Staging staging;
				rename::rename_row(value, staging, {});
				reporter.check(value.state == tasks::RowState::skipped,
				               L"an occupied destination outside the plan is skipped, not overwritten");
			}
			reporter.check(content(L"taken.txt") == "existing" &&
			               std::filesystem::exists(root / L"src.txt", error),
			               L"a skipped row leaves both files exactly where they were");

			// Anything still staged is restored when the run's staging is destroyed.
			write(L"parked.txt", "parked");
			{
				rename::Staging staging;
				reporter.check(staging.stage(root / L"parked.txt") && staging.pending() == 1,
				               L"staging moves a file to a temporary name");
				reporter.check(!std::filesystem::exists(root / L"parked.txt", error),
				               L"the original name is free while the file is staged");
			}
			reporter.check(std::filesystem::exists(root / L"parked.txt", error) &&
			               content(L"parked.txt") == "parked",
			               L"a file left staged is restored by the destructor, not only on success");

			reporter.section(L"Rename reviewed planning and sidecar cycles");
			write(L"r2.jpg", "two");
			write(L"r1.jpg", "one");
			write(L"r2.xmp", "two metadata");
			write(L"r1.xmp", "one metadata");
			auto swap = rename::analyze({root / L"r2.jpg", root / L"r1.jpg"}, L"r#", 1, tasks::CollisionPolicy::block);
			reporter.check(swap.can_run() && swap.rows.size() == 2 && swap.collisions == 0,
				L"a primary-and-sidecar exchange is reviewed as a cycle rather than a collision");
			tasks::run_plan(swap, {}, rename::run_options(swap));
			reporter.check(swap.count(tasks::RowState::success) == 2 &&
				content(L"r1.jpg") == "two" && content(L"r2.jpg") == "one" &&
				content(L"r1.xmp") == "two metadata" && content(L"r2.xmp") == "one metadata",
				L"a reviewed name swap moves both primaries and their occupied sidecars");

			write(L"keep.txt", "keep");
			auto noOp = rename::analyze({root / L"keep.txt"}, L"{name}", 1, tasks::CollisionPolicy::block);
			reporter.check(!noOp.can_run() && noOp.rows.front().state == tasks::RowState::skipped &&
				noOp.rows.front().source.native() == noOp.rows.front().destination.native(),
				L"an all-no-op review has no writing row or direction arrow");
			write(L"case.txt", "case");
			auto caseOnly = rename::analyze({root / L"case.txt"}, L"CASE", 1, tasks::CollisionPolicy::block);
			tasks::run_plan(caseOnly, {}, rename::run_options(caseOnly));
			reporter.check(caseOnly.count(tasks::RowState::success) == 1 &&
				std::ranges::any_of(std::filesystem::directory_iterator(root), [](const auto& entry)
				{
					return entry.path().filename().native() == L"CASE.txt";
				}), L"a case-only name change is staged and committed rather than mistaken for a no-op");
			std::filesystem::create_directory(root / L"folder.with.dots", error);
			auto folderNoOp = rename::analyze({root / L"folder.with.dots"}, L"{name}", 1, tasks::CollisionPolicy::block);
			reporter.check(!folderNoOp.can_run() && folderNoOp.rows.front().state == tasks::RowState::skipped,
				L"folder names keep all dots when expanding the old name");
			auto folderRename = rename::analyze({root / L"folder.with.dots"}, L"new-folder", 1, tasks::CollisionPolicy::block);
			tasks::run_plan(folderRename, {}, rename::run_options(folderRename));
			reporter.check(folderRename.count(tasks::RowState::success) == 1 &&
				std::filesystem::exists(root / L"new-folder"),
				L"renaming a dotted folder does not append a spurious extension");

			reporter.check(!rename::analyze({root / L"keep.txt"}, L"bad:name", 1, tasks::CollisionPolicy::block).can_run(),
				L"invalid generated names block the reviewed plan");
			write(L"other.txt", "other");
			auto duplicates = rename::analyze({root / L"keep.txt", root / L"other.txt"}, L"same", 1, tasks::CollisionPolicy::block);
			reporter.check(!duplicates.can_run(), L"two primary destinations cannot claim one name");
			auto unique = rename::analyze({root / L"keep.txt", root / L"other.txt"}, L"same", 1, tasks::CollisionPolicy::autoRename);
			reporter.check(unique.can_run() && !paths::equal(unique.rows[0].destination, unique.rows[1].destination),
				L"Auto-rename resolves duplicate destinations inside the plan");
			auto retained = rename::analyze({root / L"keep.txt", root / L"other.txt"}, L"keep", 1, tasks::CollisionPolicy::replace);
			reporter.check(!retained.can_run(), L"Replace cannot consume a selected original held by a no-op row");
			write(L"chain1.txt", "one");
			write(L"chain2.txt", "two");
			write(L"chain3.txt", "three");
			auto skippedChain = rename::analyze({root / L"chain1.txt", root / L"chain2.txt"}, L"chain#", 2, tasks::CollisionPolicy::skip);
			reporter.check(!skippedChain.can_run() && skippedChain.count(tasks::RowState::skipped) == 2,
				L"a skipped dependency is not advertised as a source that a preceding row can vacate");
			write(L"shared.jpg", "jpeg");
			write(L"shared.png", "png");
			write(L"shared.xmp", "shared metadata");
			reporter.check(!rename::analyze({root / L"shared.jpg", root / L"shared.png"}, L"shared-#", 1,
				tasks::CollisionPolicy::autoRename).can_run(),
				L"a companion shared by two selected primaries cannot be moved twice");

			write(L"companion.jpg", "primary");
			write(L"companion.xmp", "source metadata");
			write(L"next.xmp", "destination metadata");
			reporter.check(!rename::analyze({root / L"companion.jpg"}, L"next", 1, tasks::CollisionPolicy::block).can_run(),
				L"a sidecar-only collision blocks Rename before any primary is moved");
			auto sidecarAuto = rename::analyze({root / L"companion.jpg"}, L"next", 1, tasks::CollisionPolicy::autoRename);
			reporter.check(sidecarAuto.can_run() && sidecarAuto.rows.front().destination.filename() != L"next.jpg",
				L"Auto-rename chooses a name free for the entire companion bundle");
			auto sidecarReplace = rename::analyze({root / L"companion.jpg"}, L"next", 1, tasks::CollisionPolicy::replace);
			reporter.check(sidecarReplace.can_run() && sidecarReplace.rows.front().replace,
				L"a sidecar-only replacement requires an explicit reviewed Replace decision");
			tasks::run_plan(sidecarReplace, {}, rename::run_options(sidecarReplace));
			reporter.check(sidecarReplace.count(tasks::RowState::success) == 1 &&
				content(L"next.jpg") == "primary" && content(L"next.xmp") == "source metadata",
				L"an approved sidecar replacement commits with its primary");

			write(L"stale1.jpg", "first");
			write(L"stale2.jpg", "second");
			auto stale = rename::analyze({root / L"stale1.jpg", root / L"stale2.jpg"}, L"fresh-#", 1, tasks::CollisionPolicy::block);
			write(L"stale2.xmp", "a new companion");
			tasks::run_plan(stale, {}, rename::run_options(stale));
			reporter.check(stale.count(tasks::RowState::success) == 0 &&
				content(L"stale1.jpg") == "first" && !std::filesystem::exists(root / L"fresh-1.jpg"),
				L"a changed sidecar snapshot aborts before the first primary rename");
			auto changedSource = rename::analyze({root / L"stale1.jpg", root / L"stale2.jpg"}, L"fresh-#", 1, tasks::CollisionPolicy::block);
			write(L"stale2.jpg", "different source contents");
			tasks::run_plan(changedSource, {}, rename::run_options(changedSource));
			reporter.check(changedSource.count(tasks::RowState::success) == 0 && content(L"stale1.jpg") == "first",
				L"a modified later source prevents an earlier row from running against a stale snapshot");
			write(L"identity.txt", "same");
			auto identity = rename::analyze({root / L"identity.txt"}, L"identity-new", 1, tasks::CollisionPolicy::block);
			const auto timestamp = std::filesystem::last_write_time(root / L"identity.txt");
			std::filesystem::rename(root / L"identity.txt", root / L"identity-old.txt");
			write(L"identity.txt", "same");
			std::filesystem::last_write_time(root / L"identity.txt", timestamp);
			tasks::run_plan(identity, {}, rename::run_options(identity));
			reporter.check(identity.count(tasks::RowState::success) == 0 && content(L"identity.txt") == "same",
				L"a substituted file is stale even when its size and modification time are unchanged");

			write(L"arrival1.txt", "first");
			write(L"arrival2.txt", "second");
			auto primaryArrival = rename::analyze(
				{root / L"arrival1.txt", root / L"arrival2.txt"}, L"arrival-new-#", 1,
				tasks::CollisionPolicy::block);
			write(L"arrival-new-1.txt", "arrived");
			tasks::run_plan(primaryArrival, {}, rename::run_options(primaryArrival));
			reporter.check(primaryArrival.rows[0].state == tasks::RowState::skipped &&
				primaryArrival.rows[1].state == tasks::RowState::success &&
				content(L"arrival-new-1.txt") == "arrived" &&
				content(L"arrival-new-2.txt") == "second",
				L"a primary destination appearing after Review skips only that row and independent renames continue");

			write(L"sidecar-arrival1.jpg", "first");
			write(L"sidecar-arrival1.xmp", "first metadata");
			write(L"sidecar-arrival2.jpg", "second");
			auto sidecarArrival = rename::analyze(
				{root / L"sidecar-arrival1.jpg", root / L"sidecar-arrival2.jpg"}, L"sidecar-new-#", 1,
				tasks::CollisionPolicy::block);
			write(L"sidecar-new-1.xmp", "arrived metadata");
			tasks::run_plan(sidecarArrival, {}, rename::run_options(sidecarArrival));
			reporter.check(sidecarArrival.rows[0].state == tasks::RowState::failed &&
				sidecarArrival.rows[1].state == tasks::RowState::success &&
				content(L"sidecar-arrival1.jpg") == "first" &&
				content(L"sidecar-new-1.xmp") == "arrived metadata" &&
				content(L"sidecar-new-2.jpg") == "second",
				L"a sidecar destination appearing after Review fails only that row and independent renames continue");

			write(L"live1.jpg", "one");
			write(L"live2.jpg", "two");
			write(L"live3.jpg", "three");
			auto live = rename::analyze({root / L"live1.jpg", root / L"live2.jpg", root / L"live3.jpg"},
				L"live-new-#", 1, tasks::CollisionPolicy::block);
			auto liveOptions = rename::run_options(live);
			liveOptions.progress = [&](const size_t complete, size_t)
			{
				if (complete == 1) write(L"live2.xmp", "unreviewed metadata");
			};
			tasks::run_plan(live, {}, liveOptions);
			reporter.check(live.rows[0].state == tasks::RowState::success &&
				live.rows[1].state == tasks::RowState::failed && live.rows[2].state == tasks::RowState::success &&
				content(L"live2.jpg") == "two" && content(L"live2.xmp") == "unreviewed metadata",
				L"a new companion between rows refuses only its row and independent later rows still run");

			write(L"failure.jpg", "pixels");
			write(L"failure.xmp", "metadata");
			auto failedSidecar = row(root / L"failure.jpg", root / L"failed-out.jpg");
			failedSidecar.sidecars = {root / L"failure.xmp", root / L"failure.jpg.thm"};
			{
				rename::Staging staging;
				bool failed = false;
				try { rename::rename_row(failedSidecar, staging, {}); }
				catch (...) { failed = true; }
				reporter.check(failed && content(L"failure.jpg") == "pixels" && content(L"failure.xmp") == "metadata" &&
					!std::filesystem::exists(root / L"failed-out.jpg") && !std::filesystem::exists(root / L"failed-out.xmp"),
					L"a late companion failure restores already-moved companions and primary");
			}

			write(L"cancel2.txt", "two");
			write(L"cancel1.txt", "one");
			auto partial = rename::analyze({root / L"cancel2.txt", root / L"cancel1.txt"}, L"cancel#", 1, tasks::CollisionPolicy::block);
			std::stop_source cancelled;
			auto partialOptions = rename::run_options(partial);
			partialOptions.progress = [&cancelled](const size_t complete, size_t) { if (complete == 1) cancelled.request_stop(); };
			tasks::run_plan(partial, cancelled.get_token(), partialOptions);
			reporter.check(partial.count(tasks::RowState::success) == 1 && partial.count(tasks::RowState::notRun) == 1 &&
				content(L"cancel1.txt") == "two" && content(L"cancel1 (2).txt") == "one" &&
				!partial.rows.back().detail.empty(),
				L"cancelled cycles preserve completed rows and visibly recover staged originals under a free name");

			write(L"bundle2.jpg", "two");
			write(L"bundle1.jpg", "one");
			write(L"bundle1.xmp", "one metadata");
			write(L"bundle1 (2).jpg", "unrelated");
			auto bundleCancel = rename::analyze({root / L"bundle2.jpg", root / L"bundle1.jpg"}, L"bundle#", 1,
				tasks::CollisionPolicy::block);
			std::stop_source bundleStop;
			auto bundleOptions = rename::run_options(bundleCancel);
			bundleOptions.progress = [&bundleStop](const size_t complete, size_t) { if (complete == 1) bundleStop.request_stop(); };
			tasks::run_plan(bundleCancel, bundleStop.get_token(), bundleOptions);
			reporter.check(bundleCancel.count(tasks::RowState::success) == 1 &&
				content(L"bundle1.jpg") == "two" && content(L"bundle1 (2).jpg") == "unrelated" &&
				content(L"bundle1 (3).jpg") == "one" && content(L"bundle1 (3).xmp") == "one metadata" &&
				!std::filesystem::exists(root / L"bundle1.xmp"),
				L"cancelled exchanges stage all displaced companions and recover the bundle under one free stem");

			write(L"guard.txt", "guarded original");
			{
				rename::Staging staging;
				reporter.check(staging.stage(root / L"guard.txt"), L"the recovery fixture is staged");
				write(L"guard.txt", "new arrival");
				const auto recovered = staging.restore();
				reporter.check(recovered.size() == 1 && !recovered.front().failed && staging.pending() == 0 &&
					content(L"guard.txt") == "new arrival" && content(L"guard (2).txt") == "guarded original",
					L"bounded restoration never overwrites an unrelated arrival");
			}
		}

	}

	struct EditAccess
	{
		static void prepare(TaskEdit& task, const std::filesystem::path& path,
		                    const files::DecodedImage& image)
		{
			task.targets_ = {path, path.parent_path() / L"next.png"};
			task.set_photo(path);
			task.source_ = image;
			task.sourceSnapshot_ = files::snapshot_file(path);
			task.edits_.brightness = 20;
		}

		static bool write(TaskEdit& task, const std::filesystem::path& path, const bool inPlace)
		{
			return task.save_to(path, inPlace);
		}

		static bool dirty(const TaskEdit& task) { return task.dirty(); }
		static bool backup(const TaskEdit& task) { return task.backupOnOverwrite_; }
		static const std::filesystem::path& path(const TaskEdit& task) { return task.path_; }
		static void lose_decode(TaskEdit& task) { task.source_ = {}; }
		static bool next(TaskEdit& task) { return task.step_photo(1, true); }
		static bool offers_webp(TaskEdit& task)
		{
			task.build_controls();
			return std::ranges::count_if(task.panel_.controls(), [](const auto& control)
				{ return control.label.find(L"WebP") != std::wstring::npos; }) == 2;
		}
	};

	namespace
	{
		void test_image_edits(Reporter& reporter)
		{
			reporter.section(L"edits::ImageEdits geometry");
			constexpr sizei source{400, 300};
			edits::ImageEdits value;
			reporter.check(value.empty() && !value.is_irreversible(),
			               L"a default stack is the identity and needs no backup");
			reporter.check(edits::transformed_size(source, value) == source,
			               L"no rotation keeps the frame it was given");
			value.rotation = 1;
			reporter.check(edits::transformed_size(source, value) == sizei{300, 400},
			               L"a quarter turn swaps the sides");
			value.rotation = 2;
			reporter.check(edits::transformed_size(source, value) == source,
			               L"a half turn keeps the sides");
			value.rotation = 3;
			reporter.check(edits::transformed_size(source, value) == sizei{300, 400},
			               L"three quarter turns swap the sides");
			value.rotation = 4;
			reporter.check(edits::transformed_size(source, value) == source && !value.has_rotation(),
			               L"four quarter turns are the identity");

			value = {};
			auto bounds = edits::crop_bounds(source, value);
			reporter.check(bounds.x == 0 && bounds.y == 0 && bounds.width == 400 && bounds.height == 300,
			               L"an unstraightened photo crops to its whole frame");
			for (const int tenths : {-100, -55, -1, 1, 37, 100})
			{
				value.straighten = tenths;
				bounds = edits::crop_bounds(source, value);
				reporter.check(bounds.width > 0 && bounds.height > 0 && bounds.width <= 400 &&
				               bounds.height <= 300 && bounds.x >= 0 && bounds.y >= 0 &&
				               bounds.right() <= 400 && bounds.bottom() <= 300,
				               L"a straightened crop stays inside the picture at every angle");
			}
			value.straighten = 100;
			const auto tilted = edits::crop_bounds(source, value);
			reporter.check(tilted.width < 400 && tilted.height < 300,
			               L"straightening shrinks the usable rectangle");
			reporter.check(std::abs(tilted.x * 2 + tilted.width - 400) <= 1 &&
			               std::abs(tilted.y * 2 + tilted.height - 300) <= 1,
			               L"the usable rectangle stays centred in the frame");

			value = {};
			value.crop = {10, 10, 1000, 1000};
			const auto clamped = edits::effective_crop(source, value);
			reporter.check(clamped.width == 400 && clamped.height == 300,
			               L"a crop larger than the picture is pulled back inside it");

			bool cornersInside = true;
			for (const auto size : {sizei{400, 300}, sizei{301, 400}, sizei{17, 129}, sizei{4, 3}})
				for (const int angle : {-100, -55, -1, 1, 37, 100})
					for (int turn = 0; turn < 4; ++turn)
					{
						edits::ImageEdits geometry;
						geometry.straighten = angle;
						geometry.rotation = turn;
						const auto frame = edits::transformed_size(size, geometry);
						const auto crop = edits::crop_bounds(size, geometry);
						const double cx = (frame.width - 1) * 0.5;
						const double cy = (frame.height - 1) * 0.5;
						const double radians = angle * 0.1 * 3.14159265358979323846 / 180.0;
						for (const int y : {crop.y, crop.bottom() - 1})
							for (const int x : {crop.x, crop.right() - 1})
							{
								const double sx = cx + (x - cx) * std::cos(radians) + (y - cy) * std::sin(radians);
								const double sy = cy - (x - cx) * std::sin(radians) + (y - cy) * std::cos(radians);
								cornersInside = cornersInside && crop.width > 0 && crop.height > 0 &&
									sx >= -1e-8 && sy >= -1e-8 && sx <= frame.width - 1 + 1e-8 &&
									sy <= frame.height - 1 + 1e-8;
							}
					}
			reporter.check(cornersInside,
				L"every crop corner inverse-maps between real pixel centres, including narrow and odd frames");

			files::DecodedImage previewImage;
			previewImage.width = 400;
			previewImage.height = 300;
			previewImage.originalWidth = 4000;
			previewImage.originalHeight = 3000;
			previewImage.pixels.assign(400 * 300, 0xff123456u);
			edits::PreviewSource cache;
			reporter.check(cache.update(previewImage, {200, 150}) &&
				cache.image().width == 200 && cache.image().originalWidth == 4000,
				L"the preview scales pixels but retains full-resolution dimensions");
			const auto* retained = cache.image().pixels.data();
			reporter.check(!cache.update(previewImage, {200, 150}) &&
				cache.image().pixels.data() == retained,
				L"unchanged pane dimensions reuse the retained scaled source");
			reporter.check(cache.update(previewImage, {100, 100}) &&
				cache.image().width == 100 && cache.image().height == 75,
				L"a changed pane rebuilds the scaled source with its aspect ratio");
			cache.reset();
			reporter.check(cache.update(previewImage, {100, 100}),
				L"loading a different photo invalidates the scaled source even at the same pane size");

			value = {};
			value.rotation = 1;
			value.crop = {30, 40, 120, 200};
			const auto scaled = edits::scaled_edits(value, {400, 300}, {100, 75});
			reporter.check(rect_equals(scaled.crop, {7, 10, 30, 50}),
				L"preview crop coordinates scale in the rotated frame, not the original orientation");
		}

		void test_image_edit_pixels(Reporter& reporter)
		{
			reporter.section(L"edits::apply");
			files::DecodedImage photo;
			photo.width = 4;
			photo.height = 3;
			photo.originalWidth = 4;
			photo.originalHeight = 3;
			photo.pixels.resize(12);
			for (size_t index = 0; index < photo.pixels.size(); ++index)
				photo.pixels[index] = 0xff000000u | static_cast<std::uint32_t>(index) * 0x00010203u;

			const edits::ImageEdits identity;
			const auto untouched = edits::apply(photo, identity);
			reporter.check(untouched.width == 4 && untouched.height == 3 &&
			               untouched.pixels == photo.pixels,
			               L"an identity stack leaves every pixel bit-identical");

			edits::ImageEdits quarter;
			quarter.rotation = 1;
			auto turned = photo;
			for (int turn = 0; turn < 4; ++turn) turned = edits::apply(turned, quarter);
			reporter.check(turned.width == 4 && turned.height == 3 && turned.pixels == photo.pixels,
			               L"four quarter turns return the original pixels exactly");

			const auto once = edits::apply(photo, quarter);
			reporter.check(once.width == 3 && once.height == 4,
			               L"one quarter turn transposes the picture");
			reporter.check(once.pixels[0] == photo.pixels[8],
			               L"a clockwise turn puts the bottom-left pixel top-left");

			edits::ImageEdits cropped;
			cropped.crop = {1, 1, 2, 2};
			const auto cut = edits::apply(photo, cropped);
			reporter.check(cut.width == 2 && cut.height == 2 && cut.pixels[0] == photo.pixels[5],
			               L"a crop takes exactly the reviewed rectangle");

			for (const auto control : {
				     &edits::ImageEdits::brightness, &edits::ImageEdits::contrast, &edits::ImageEdits::darks,
				     &edits::ImageEdits::midtones, &edits::ImageEdits::lights, &edits::ImageEdits::saturation,
				     &edits::ImageEdits::vibrance, &edits::ImageEdits::temperature, &edits::ImageEdits::tint
			     })
			{
				edits::ImageEdits colour;
				auto pixels = photo.pixels;
				edits::apply_color(pixels, colour);
				reporter.check(pixels == photo.pixels, L"every colour control at zero is a no-op");
				for (const int extreme : {-100, 100})
				{
					auto extremePixels = photo.pixels;
					colour.*control = extreme;
					edits::apply_color(extremePixels, colour);
					reporter.check(std::ranges::all_of(extremePixels, [](const std::uint32_t pixel)
					               {
						               return (pixel & 0xff000000u) == 0xff000000u;
					               }),
					               L"a colour control at its extreme keeps the alpha channel");
				}
			}

			// Clamping, not wrapping: a channel pushed past the end stops at the end.
			const auto channels = [](const std::uint32_t pixel)
			{
				return std::array{pixel & 0xff, pixel >> 8 & 0xff, pixel >> 16 & 0xff};
			};
			files::DecodedImage extremes;
			extremes.width = 2;
			extremes.height = 1;
			extremes.pixels = {0xff000000u, 0xffffffffu};
			for (const int direction : {-100, 100})
			{
				edits::ImageEdits colour;
				colour.brightness = direction;
				auto moved = photo.pixels;
				edits::apply_color(moved, colour);
				bool ordered = true;
				for (size_t index = 0; index < moved.size(); ++index)
				{
					const auto after = channels(moved[index]);
					const auto before = channels(photo.pixels[index]);
					for (size_t channel = 0; channel < after.size(); ++channel)
						ordered = ordered && (direction > 0
							                      ? after[channel] >= before[channel]
							                      : after[channel] <= before[channel]);
				}
				reporter.check(ordered,
				               L"full brightness only ever moves a channel towards its own limit");

				auto ends = extremes.pixels;
				edits::apply_color(ends, colour);
				reporter.check(direction > 0 ? ends[1] == 0xffffffffu : ends[0] == 0xff000000u,
				               L"a channel already at its limit stays there rather than wrapping");
			}

			edits::ImageEdits straightened;
			straightened.straighten = 50;
			const auto warped = edits::apply(photo, straightened, false);
			reporter.check(warped.width == 4 && warped.height == 3,
			               L"a warp keeps the frame it was given");
			const auto saved = edits::apply(photo, straightened);
			reporter.check(saved.width <= 4 && saved.height <= 3 && saved.width > 0,
			               L"saving a straightened photo crops the emptied corners away");
			reporter.check(std::ranges::all_of(saved.pixels,
				[](const auto pixel) { return (pixel >> 24) == 255; }),
				L"the final straightened crop contains no partly transparent corner pixels");

			files::DecodedImage alpha;
			alpha.width = 16;
			alpha.height = 16;
			alpha.pixels.assign(256, 0x00ff0000u);
			for (int y = 0; y < 16; ++y)
				for (int x = 0; x < 8; ++x)
					alpha.pixels[static_cast<size_t>(y) * 16 + x] = 0xff0000ffu;
			const auto alphaWarp = edits::apply(alpha, straightened, false);
			reporter.check(std::ranges::all_of(alphaWarp.pixels, [](const auto pixel)
				{ return (pixel >> 24) == 0 || (pixel & 0x00ffffffu) == 0x000000ffu; }),
				L"straightening interpolates alpha-weighted colour without bleeding invisible red pixels");
			edits::PreviewSource alphaPreview;
			alphaPreview.update(alpha, {3, 3});
			reporter.check(std::ranges::all_of(alphaPreview.image().pixels, [](const auto pixel)
				{ return (pixel >> 24) == 0 || (pixel & 0x00ffffffu) == 0x000000ffu; }),
				L"scaled previews do not bleed invisible colour into translucent pixels");

			auto malformed = photo;
			malformed.pixels.resize(1);
			reporter.check(edits::apply(malformed, quarter).pixels.empty(),
				L"an incomplete pixel buffer is rejected before rotate or crop reads it");
		}

		void test_auto_adjustments(Reporter& reporter)
		{
			reporter.section(L"edits::auto_straighten / auto_color");

			// A tilt the recovered angle has to undo. The sample point is rotated rather than the
			// picture, so the bands stay hard edged instead of being resampled into gradients: a
			// button whose vote always lands in the zero bucket is a button that does nothing.
			for (const int tenths : {-30, -15, 15, 30})
			{
				files::DecodedImage banded;
				banded.width = 320;
				banded.height = 240;
				banded.originalWidth = 320;
				banded.originalHeight = 240;
				banded.pixels.resize(static_cast<size_t>(320) * 240);
				const double angle = tenths / 10.0 * 3.14159265358979323846 / 180.0;
				const double cosine = std::cos(angle);
				const double sine = std::sin(angle);
				for (int y = 0; y < 240; ++y)
					for (int x = 0; x < 320; ++x)
					{
						const double distance = -(x - 160.0) * sine + (y - 120.0) * cosine;
						const auto band = static_cast<int>(std::floor(distance / 16.0));
						const std::uint32_t level = band & 1 ? 230 : 25;
						banded.pixels[static_cast<size_t>(y) * 320 + x] =
							0xff000000u | level << 16 | level << 8 | level;
					}
				edits::ImageEdits recovered;
				reporter.check(edits::auto_straighten(banded, recovered),
				               L"a picture full of straight edges can be straightened automatically");
				reporter.check(recovered.straighten != 0 &&
				               std::abs(recovered.straighten + tenths) <= 3,
				               L"auto straighten recovers a known tilt to within a third of a degree");
			}

			files::DecodedImage flat;
			flat.width = 32;
			flat.height = 32;
			flat.pixels.assign(static_cast<size_t>(32) * 32, 0xff808080u);
			edits::ImageEdits untouched;
			reporter.check(!edits::auto_straighten(flat, untouched) && untouched.straighten == 0,
			               L"a picture with no edges is refused rather than tilted at random");

			// The curve stretches about mid grey and adds brightness after it, so the offset that
			// carries the midpoint up has to be scaled by the contrast gain. Without that factor an
			// underexposed picture came back underexposed by exactly the gain.
			files::DecodedImage dim;
			dim.width = 64;
			dim.height = 64;
			dim.originalWidth = 64;
			dim.originalHeight = 64;
			dim.pixels.resize(static_cast<size_t>(64) * 64);
			for (int y = 0; y < 64; ++y)
				for (int x = 0; x < 64; ++x)
				{
					const auto level = static_cast<std::uint32_t>(10 + x * 145 / 63);
					dim.pixels[static_cast<size_t>(y) * 64 + x] =
						0xff000000u | level << 16 | level << 8 | level;
				}
			edits::ImageEdits lifted;
			reporter.check(edits::auto_color(dim, lifted), L"auto colour reads the picture's histogram");
			reporter.check(lifted.contrast > 0 && lifted.contrast < 100,
			               L"an underexposed ramp asks for contrast the slider can actually give");
			reporter.check(lifted.brightness > 0, L"an underexposed picture is asked to be brighter");
			auto corrected = dim.pixels;
			edits::apply_color(corrected, lifted);
			double mean = 0;
			for (const std::uint32_t pixel : corrected) mean += pixel >> 8 & 0xff;
			mean /= corrected.size();
			reporter.check(mean > 112 && mean < 144,
			               L"auto colour carries an underexposed picture to mid grey");

			// Grey-world balance: a blue cast comes back closer to neutral, not further from it.
			files::DecodedImage cast;
			cast.width = 32;
			cast.height = 32;
			cast.pixels.assign(static_cast<size_t>(32) * 32, 0xff4080c0u);
			edits::ImageEdits balanced;
			edits::auto_color(cast, balanced);
			reporter.check(balanced.temperature > 0, L"a blue cast asks for a warmer temperature");
			edits::ImageEdits temperatureOnly;
			temperatureOnly.temperature = balanced.temperature;
			auto neutral = cast.pixels;
			edits::apply_color(neutral, temperatureOnly);
			const int beforeGap = static_cast<int>(0xc0) - static_cast<int>(0x40);
			const int afterGap = std::abs(static_cast<int>(neutral[0] & 0xff) -
				static_cast<int>(neutral[0] >> 16 & 0xff));
			reporter.check(afterGap < beforeGap, L"the balance closes the cast rather than widening it");

			edits::ImageEdits nothing;
			files::DecodedImage empty;
			reporter.check(!edits::auto_color(empty, nothing) && !edits::auto_straighten(empty, nothing),
			               L"an empty picture leaves every control alone");
			auto malformed = flat;
			malformed.pixels.resize(1);
			reporter.check(!edits::auto_color(malformed, nothing) && !edits::auto_straighten(malformed, nothing),
				L"automatic adjustments reject truncated image buffers");

			auto invisible = dim;
			for (auto& pixel : invisible.pixels) pixel &= 0x00ffffffu;
			nothing.brightness = 17;
			nothing.straighten = 12;
			reporter.check(!edits::auto_color(invisible, nothing) && nothing.brightness == 17 &&
				!edits::auto_straighten(invisible, nothing) && nothing.straighten == 12,
				L"invisible colours neither bias the histogram nor manufacture straightening edges");

			files::DecodedImage single;
			single.width = single.height = 1;
			single.pixels = {0xff808080u};
			nothing.contrast = 60;
			reporter.check(edits::auto_color(single, nothing) && nothing.contrast == 0 && nothing.brightness == 0,
				L"a one-pixel neutral histogram has no invented zero percentile or stale contrast");

			auto redRamp = dim;
			for (size_t index = 0; index < redRamp.pixels.size(); ++index)
				redRamp.pixels[index] = 0xff005000u | static_cast<std::uint32_t>(index % 256) << 16;
			edits::ImageEdits lumaAdjustment;
			reporter.check(edits::auto_color(redRamp, lumaAdjustment) && lumaAdjustment.contrast > 0,
				L"auto colour measures luminance rather than mistaking a constant green channel for a flat image");
		}

		void test_edit_naming(Reporter& reporter)
		{
			reporter.section(L"Save as naming");
			reporter.check(platform::run_with_status({}, L"Edit test", L"Saving test image...", [] { return true; }),
				L"modal status joins a successful task-queue work item without requiring a UI completion message");
			reporter.check(!platform::run_with_status({}, L"Edit test", L"Saving test image...", [] { return false; }),
				L"modal status propagates a failed save result");
			bool workerException = false;
			try
			{
				platform::run_with_status({}, L"Edit test", L"Saving test image...", []() -> bool
				{
					throw std::runtime_error("save failed");
				});
			}
			catch (const std::runtime_error&) { workerException = true; }
			reporter.check(workerException &&
				platform::run_with_status({}, L"Edit test", L"Saving test image...", [] { return true; }),
				L"worker exceptions return to the caller and do not strand completion or retire the save queue");
			std::error_code error;
			const auto root = files::unique_destination(std::filesystem::current_path() / L"iw30-edit-tests");
			if (root.empty())
			{
				reporter.check(false, L"the edit fixture directory can be reserved");
				return;
			}
			std::filesystem::create_directories(root, error);
			if (error)
			{
				reporter.check(false, L"the edit fixture directory can be created");
				return;
			}
			const auto write = [&root](const std::wstring& name)
			{
				std::ofstream(root / name, std::ios::binary).put('x');
			};
			write(L"photo.jpg");
			reporter.check(proposed_edit_path(root / L"photo.jpg").filename() == L"photo-edit.jpg",
			               L"Save as proposes a sibling ending in -edit");
			write(L"photo-edit.jpg");
			reporter.check(proposed_edit_path(root / L"photo.jpg").filename() == L"photo-edit 2.jpg",
			               L"an occupied -edit name moves on to -edit 2");
			write(L"photo-edit 2.jpg");
			reporter.check(proposed_edit_path(root / L"photo.jpg").filename() == L"photo-edit 3.jpg",
			               L"Save as never proposes an existing name");

			const auto backup = files::create_original_backup(root / L"photo.jpg");
			reporter.check(backup.filename() == L"photo-original.jpg" &&
			               std::filesystem::exists(backup, error) &&
			               std::filesystem::exists(root / L"photo.jpg", error),
			               L"an original backup is written beside the photo and keeps the photo");
			const auto second = files::create_original_backup(root / L"photo.jpg");
			reporter.check(second.filename() == L"photo-original (2).jpg",
			               L"a second backup never replaces the first");
			reporter.check(files::create_original_backup(root / L"missing.jpg").empty(),
			               L"backing up a file that is not there writes nothing");
			reporter.check(proposed_edit_path({}).empty(), L"an empty source never proposes an unrelated -edit file");
			std::filesystem::create_directories(root / L"folder-edit.png", error);
			reporter.check(proposed_edit_path(root / L"folder.png").filename() == L"folder-edit 2.png",
				L"Save as also skips directories with the proposed filename");
			const auto identityPath = root / L"identity.dat";
			std::ofstream(identityPath, std::ios::binary) << "same";
			const auto identity = files::snapshot_file(identityPath);
			const auto timestamp = std::filesystem::last_write_time(identityPath);
			std::filesystem::rename(identityPath, root / L"old-identity.dat", error);
			std::ofstream(identityPath, std::ios::binary) << "same";
			std::filesystem::last_write_time(identityPath, timestamp, error);
			reporter.check(identity && !files::matches_snapshot(identityPath, *identity),
				L"a replacement source is detected even when its size and modification time are unchanged");

			files::DecodedImage image;
			image.width = 20;
			image.height = 12;
			image.originalWidth = 20;
			image.originalHeight = 12;
			image.pixels.assign(20 * 12, 0xff405060u);
			const auto source = root / L"source.png";
			const bool sourceSaved = files::save_image(image, source, files::ImageSaveFormat::png);
			reporter.check(sourceSaved, L"the edit save fixture can be encoded");
			if (sourceSaved)
			{
				bool checkedAtCommit = false;
				auto changed = image;
				changed.pixels.assign(changed.pixels.size(), 0xffabcdefu);
				reporter.check(!files::save_image(changed, source, files::ImageSaveFormat::png, {}, true, [&]
					{
						checkedAtCommit = true;
						return false;
					}) && checkedAtCommit && files::load_image(source).pixels == image.pixels,
					L"a rejected final commit leaves the previous encoded image intact");
				const auto racedDestination = root / L"arrived-during-save.png";
				reporter.check(!files::save_image(image, racedDestination, files::ImageSaveFormat::png, {}, false, [&]
					{
						std::ofstream(racedDestination, std::ios::binary) << "new arrival";
						return true;
					}) && std::filesystem::file_size(racedDestination) == 11,
					L"an unreviewed destination created during encoding is never replaced");
				const auto beforeChange = files::snapshot_file(source);
				reporter.check(beforeChange &&
					!files::save_image(changed, source, files::ImageSaveFormat::png, {}, true, [&]
					{
						std::filesystem::last_write_time(source,
							std::filesystem::last_write_time(source) + std::chrono::seconds(5), error);
						return files::matches_snapshot(source, *beforeChange);
					}) && files::load_image(source).pixels == image.pixels,
					L"a source changed immediately before replacement refuses the encoded edit");
				TaskEdit editor;
				EditAccess::prepare(editor, source, image);
				reporter.check(EditAccess::backup(editor),
					L"original backups are enabled even before the options panel is created");
				const auto formats = files::writable_image_formats();
				const bool webpAvailable = std::ranges::find(formats, files::ImageSaveFormat::webp) != formats.end();
				TaskEdit optionsEditor;
				EditAccess::prepare(optionsEditor, source, image);
				reporter.check(EditAccess::offers_webp(optionsEditor) == webpAvailable,
					L"WebP quality and lossless controls appear only when a runtime encoder is available");
				if (!webpAvailable)
					reporter.check(!EditAccess::write(editor, root / L"unavailable.webp", false) &&
						!std::filesystem::exists(root / L"unavailable.webp", error),
						L"an unavailable WebP encoder refuses the save without writing another format under its extension");
				reporter.check(!EditAccess::write(editor, source, false) && EditAccess::dirty(editor) &&
					paths::equal(EditAccess::path(editor), source),
					L"Save as cannot select the original file and leaves its draft and target intact");
				reporter.check(!EditAccess::write(editor, root / L"wrong.gif", false) &&
					!std::filesystem::exists(root / L"wrong.gif", error),
					L"an unrecognised extension never receives silently encoded PNG pixels");

				const auto reviewedTime = std::filesystem::last_write_time(source);
				const auto replacedSource = root / L"replaced-source.png";
				std::filesystem::rename(source, replacedSource, error);
				const auto copiedSource = files::copy_file_to(replacedSource, source, false);
				std::filesystem::last_write_time(source, reviewedTime, error);
				reporter.check(!copiedSource && !error && !EditAccess::write(editor, source, true) &&
					EditAccess::dirty(editor) && paths::equal(EditAccess::path(editor), source) &&
					!std::filesystem::exists(root / L"source-original.png", error),
					L"replacing the source with identical bytes and time still vetoes Save without losing or navigating the draft");
				EditAccess::prepare(editor, source, image);
				std::filesystem::last_write_time(source,
					std::filesystem::last_write_time(source) + std::chrono::seconds(5), error);
				reporter.check(!EditAccess::write(editor, source, true) && EditAccess::dirty(editor) &&
					!std::filesystem::exists(root / L"source-original.png", error),
					L"a changed source refuses save before creating a backup and keeps the draft");

				EditAccess::prepare(editor, source, image);
				std::filesystem::permissions(source, std::filesystem::perms::owner_read,
					std::filesystem::perm_options::replace, error);
				reporter.check(!error && !EditAccess::write(editor, source, true) && EditAccess::dirty(editor),
					L"a read-only source cannot be replaced by an in-place edit");
				std::filesystem::permissions(source, std::filesystem::perms::owner_all,
					std::filesystem::perm_options::replace, error);
				EditAccess::prepare(editor, source, image);
				reporter.check(EditAccess::write(editor, source, true),
					L"a valid in-place edit writes with default backup protection");
				const auto original = files::load_image(root / L"source-original.png");
				reporter.check(original.width == 20 && original.height == 12 && original.pixels == image.pixels,
					L"the original backup contains the unchanged full-resolution source pixels");

				auto preview = image;
				preview.width = preview.height = 2;
				preview.pixels.resize(4);
				EditAccess::prepare(editor, source, preview);
				const auto destination = root / L"full-edit.png";
				reporter.check(EditAccess::write(editor, destination, false),
					L"Save as renders from the source file rather than the small preview");
				const auto full = files::load_image(destination);
				reporter.check(full.width == 20 && full.height == 12,
					L"saved output retains full resolution when the preview is only two pixels wide");

				const auto invalid = root / L"invalid.png";
				std::ofstream(invalid, std::ios::binary) << "not an image";
				EditAccess::prepare(editor, invalid, preview);
				reporter.check(!EditAccess::write(editor, root / L"invalid-edit.png", false) &&
					EditAccess::dirty(editor) && !std::filesystem::exists(root / L"invalid-edit.png", error),
					L"a failed full decode never saves the scaled preview or discards the draft");
				EditAccess::lose_decode(editor);
				reporter.check(!EditAccess::next(editor) && EditAccess::dirty(editor) &&
					paths::equal(EditAccess::path(editor), invalid),
					L"Save and open next cannot advance when a dirty photo is not available to save");
				editor.set_photo(destination);
				reporter.check(paths::equal(EditAccess::path(editor), destination),
					L"a newly saved path cannot be replaced by the first old selector item");
			}
			std::filesystem::remove_all(root, error);
		}

		void test_control_panel(Reporter& reporter)
		{
			reporter.section(L"ui::ControlPanel");
			ui::ControlPanel panel;
			panel.set_dpi(96);
			int changes = 0;
			const auto counted = [&changes] { ++changes; };

			ui::Control first;
			first.id = 1;
			first.kind = ui::ControlKind::radio;
			first.label = L"First";
			first.group = 7;
			first.checked = true;
			first.changed = counted;
			panel.add(std::move(first));

			ui::Control second;
			second.id = 2;
			second.kind = ui::ControlKind::radio;
			second.label = L"Second";
			second.group = 7;
			second.changed = counted;
			panel.add(std::move(second));

			ui::Control slider;
			slider.id = 3;
			slider.kind = ui::ControlKind::slider;
			slider.label = L"Quality";
			slider.minimum = 0;
			slider.maximum = 100;
			slider.value = 50;
			slider.changed = counted;
			panel.add(std::move(slider));

			ui::Control unrelated;
			unrelated.id = 4;
			unrelated.kind = ui::ControlKind::radio;
			unrelated.label = L"Other group";
			unrelated.group = 8;
			unrelated.checked = true;
			panel.add(std::move(unrelated));

			panel.set_checked(2, true);
			reporter.check(panel.find(2)->checked && !panel.find(1)->checked,
			               L"choosing a radio clears the others in its group");
			reporter.check(panel.find(4)->checked, L"another radio group is left alone");

			panel.set_value(3, 500);
			reporter.check(panel.find(3)->value == 100, L"a slider value is clamped to its range");
			panel.set_value(3, -20);
			reporter.check(panel.find(3)->value == 0, L"a slider value is clamped at the minimum");

			panel.set_enabled(3, false);
			reporter.check(!panel.find(3)->enabled, L"a control can be disabled");
			reporter.check(panel.find(99) == nullptr, L"an unknown control id finds nothing");

			// Hit testing needs laid-out bounds, which only a renderer can supply, so the
			// geometry is set directly here.
			panel.find(1)->hit = {0, 0, 100, 20};
			panel.find(2)->hit = {0, 20, 100, 20};
			panel.set_enabled(3, true);
			panel.find(3)->hit = {0, 40, 100, 20};
			changes = 0;
			reporter.check(panel.mouse_down({50, 5}), L"a click inside a control is handled");
			reporter.check(panel.find(1)->checked && !panel.find(2)->checked && changes == 1,
			               L"clicking a radio selects it and reports one change");
			changes = 0;
			panel.mouse_down({50, 5});
			reporter.check(changes == 0, L"clicking the radio that is already on reports no change");
			reporter.check(!panel.mouse_down({500, 500}), L"a click outside every control is not handled");
			panel.mouse_down({95, 45});
			reporter.check(panel.find(3)->value > 80 && panel.dragging(),
			               L"pressing near the right of a slider sets a high value and starts a drag");
			panel.mouse_up({95, 45});
			reporter.check(!panel.dragging(), L"releasing the mouse ends the drag");

			panel.focus(0);
			reporter.check(panel.focus_next() && panel.focused_id() == 1,
				L"Tab focus begins with the first interactive control");
			panel.set_enabled(2, false);
			reporter.check(panel.focus_next() && panel.focused_id() == 3,
				L"Tab focus skips disabled controls");
			platform::KeyInput key;
			key.key = platform::KeyCode::home;
			panel.key(key);
			key.key = platform::KeyCode::right;
			panel.key(key);
			reporter.check(panel.find(3)->value == 1,
				L"a focused slider supports Home and precise arrow-key adjustment");
			key.key = platform::KeyCode::end;
			panel.key(key);
			key.key = platform::KeyCode::right;
			reporter.check(panel.key(key) && panel.find(3)->value == 100,
				L"a slider consumes arrow keys at its bound instead of scrolling the review");
			panel.focus(1);
			key.key = platform::KeyCode::space;
			changes = 0;
			panel.key(key);
			reporter.check(changes == 0, L"Space on the selected radio does not spuriously reanalyze");
			panel.set_enabled(2, true);
			panel.focus(2);
			panel.key(key);
			reporter.check(panel.find(2)->checked && !panel.find(1)->checked && changes == 1,
				L"keyboard activation selects a radio exclusively and notifies once");
			key.key = platform::KeyCode::left;
			panel.key(key);
			reporter.check(panel.focused_id() == 1 && panel.find(1)->checked && !panel.find(2)->checked &&
				panel.find(4)->checked, L"radio arrow navigation stays inside its exclusive group");

			ui::Control choice;
			choice.id = 5;
			choice.kind = ui::ControlKind::choice;
			choice.choices = {L"First", L"Second", L"Third"};
			choice.changed = counted;
			panel.add(std::move(choice));
			panel.focus(5);
			key.key = platform::KeyCode::end;
			panel.key(key);
			reporter.check(panel.find(5)->value == 2, L"choices can be selected without opening a mouse popup");
			key.key = platform::KeyCode::up;
			panel.key(key);
			reporter.check(panel.find(5)->value == 1, L"Up selects the preceding choice in display order");
			reporter.check(!panel.focus_next() && panel.focused_id() == 0,
				L"Tab past the final control releases focus to the review surface");
			reporter.check(panel.focus_next(true) && panel.focused_id() == 5,
				L"Shift-Tab enters the controls at their last interactive row");

			panel.mouse_down({50, 45});
			panel.set_interactive(false);
			changes = 0;
			reporter.check(!panel.dragging() && !panel.mouse_down({50, 5}) && !panel.key(key) && changes == 0,
				L"a running task disables keyboard and mouse changes and ends an active slider drag");
			panel.set_interactive(true);
			panel.mouse_down({50, 45});
			panel.set_enabled(3, false);
			reporter.check(!panel.dragging(), L"disabling the dragged control cancels its mouse state");

			panel.set_enabled(3, true);
			panel.find(3)->minimum = -2000000000;
			panel.find(3)->maximum = 2000000000;
			panel.mouse_down({95, 45});
			reporter.check(panel.find(3)->value == 2000000000,
				L"wide slider ranges do not overflow during pointer-value conversion");
			panel.mouse_up({});

			ui::Control rebuilding;
			rebuilding.id = 6;
			rebuilding.kind = ui::ControlKind::checkBox;
			rebuilding.hit = {0, 100, 100, 20};
			rebuilding.changed = [&panel, &changes]
			{
				panel.clear();
				++changes;
				ui::Control replacement;
				replacement.id = 7;
				replacement.kind = ui::ControlKind::button;
				panel.add(std::move(replacement));
			};
			panel.add(std::move(rebuilding));
			changes = 0;
			panel.mouse_down({20, 105});
			reporter.check(changes == 1 && panel.find(7) && panel.focused_id() == 0 &&
				panel.hot_id() == 0 && !panel.dragging(),
				L"a change callback can rebuild the panel without destroying the executing callback or keeping stale focus");
		}
	}

	int run_all()
	{
		Reporter reporter;
		emit(L"ImageWalker 3.0 self-tests\n");

		const auto root = files::unique_destination(std::filesystem::current_path() / L"iw30-suite-history");
		std::error_code error;
		const bool created = !root.empty() && std::filesystem::create_directory(root, error);
		reporter.check(created && !error, L"the test suite reserves its own undo storage");
		if (!created || error) return reporter.failures();
		{
			struct Cleanup
			{
				std::filesystem::path root;
				Reporter& reporter;
				~Cleanup()
				{
					std::error_code failure;
					std::filesystem::remove_all(root, failure);
					reporter.check(!failure, L"all test-owned undo recovery data is removed after its history closes");
				}
			} cleanup{root, reporter};
			undo::History fixture(root / L"history");
			undo::ScopedHistory selected(fixture);
			MainWindowAccess noFileHistory;
			MainWindowAccess withFileHistory(&fixture);
			reporter.check(!noFileHistory.can_undo() && withFileHistory.can_undo() == fixture.can_undo(),
				L"file Undo is an explicit window dependency rather than an uninitialized global lookup");

			test_startup_dispatch(reporter);
			test_mode_bar(reporter);
			test_main_window_host(reporter);
			test_color(reporter);
			test_dpi_metrics(reporter);
			test_supported_images(reporter);
			test_selection_model(reporter);
			test_marquee(reporter);
			test_collection_state(reporter);
			test_navigation_history(reporter);
			test_parent_folder(reporter);
			test_geometry(reporter);
			test_collage_layout(reporter);
			test_flex_layout(reporter);
			test_vertical_scroll(reporter);
			test_zoom_model(reporter);
			test_loading_model(reporter);
			test_image_resampler(reporter);
			test_pixel_blending(reporter);
			test_folder_scan(reporter);
			test_folder_items(reporter);
			test_image_downsample(reporter);
			test_image_save_options(reporter);
			test_task_plan(reporter);
			test_task_run_plan(reporter);
			test_task_runner(reporter);
			test_file_services(reporter);
			test_media(reporter);
			test_rename_tokens(reporter);
			test_convert_sizing(reporter);
			test_tools_configuration(reporter);
			test_rename_staging(reporter);
			test_image_edits(reporter);
			test_image_edit_pixels(reporter);
			test_photo_geometry(reporter);
			test_document_detection(reporter);
			test_bundled_webp(reporter);
			test_auto_adjustments(reporter);
			test_edit_naming(reporter);
			test_control_panel(reporter);
			test_sync_workflow(reporter);
			test_undo(reporter);
		}

		emit(std::format(L"[ DONE ] {} checks, {} failure(s)\n", reporter.total(), reporter.failures()));
		return reporter.failures();
	}
}
