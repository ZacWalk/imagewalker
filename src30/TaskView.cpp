// ImageWalker by Zac Walker
// Implements the shared task surface that replaces Items while a task is open.

#include "TaskView.h"

#include <algorithm>
#include <format>
#include <utility>

namespace iw
{
	namespace
	{
		constexpr int panelWidth = 300;
		constexpr int thumbnailEdge = 56;
		constexpr size_t maximumThumbnails = 12;

		const wchar_t* state_text(const tasks::RowState state)
		{
			switch (state)
			{
			case tasks::RowState::success: return L"Success";
			case tasks::RowState::failed: return L"Failed";
			case tasks::RowState::skipped: return L"Skipped";
			case tasks::RowState::notRun: return L"Not run";
			case tasks::RowState::blocked: return L"Blocked";
			case tasks::RowState::running: return L"Working...";
			default: return L"";
			}
		}
	}

	TaskView::TaskView()
		: TaskView(tasks::TaskRunner::Dispatcher{})
	{
	}

	TaskView::TaskView(tasks::TaskRunner::Dispatcher dispatcher)
		: runner_(std::move(dispatcher))
	{
		plan_.blockReason = L"Analyze to review the changes.";
	}

	TaskView::~TaskView()
	{
		runner_.invalidate();
	}

	bool TaskView::create(const platform::WindowFramePtr& parent, Host host)
	{
		if (!parent) return false;
		host_ = std::move(host);
		platform::WindowOptions options;
		options.className = "ImageWalker30.Task";
		options.child = true;
		options.visible = false;
		options.clipChildren = true;
		options.eraseBackground = false;
		frame_ = parent->create_child(shared_from_this(), options);
		if (!frame_) return false;
		dpi_ = frame_->dpi();
		panel_.set_dpi(dpi_);
		panel_.set_invalidate_handler([this] { invalidate(); });
		panel_.set_activate_handler([this](ui::Control& control) { control_activated(control); });
		columns_ = review_columns();

		runner_.progress = [this](const tasks::TaskRunner::Phase phase, const size_t complete,
		                          const size_t total)
		{
			const auto keepAlive = shared_from_this();
			const auto generation = runner_.generation();
			completed_ = complete;
			total_ = total;
			if (host_.progress) host_.progress(complete, total);
			if (generation != runner_.generation()) return;
			set_status(cancelling_ ? L"Cancelling..." : phase == tasks::TaskRunner::Phase::analyzing
				           ? L"Analyzing..."
				           : std::format(L"Processing... {} of {}", complete, total));
		};
		runner_.rowProgress = [this](const size_t index, const tasks::TaskRow& row)
		{
			if (index >= plan_.rows.size()) return;
			plan_.rows[index] = row;
			if (row.state == tasks::RowState::running)
			{
				runningRow_ = index;
				ensure_row_visible(index);
			}
			else if (runningRow_ == index) runningRow_ = plan_.rows.size();
			invalidate();
		};
		runner_.analyzed = [this](tasks::TaskPlan plan)
		{
			const auto keepAlive = shared_from_this();
			const auto generation = runner_.generation();
			plan_ = std::move(plan);
			analyzing_ = false;
			cancelling_ = false;
			finished_ = false;
			columns_ = review_columns();
			set_status(analysis_summary());
			if (generation != runner_.generation()) return;
			update_command_state();
			if (generation != runner_.generation()) return;
			if (host_.progress) host_.progress(0, 0);
			invalidate();
			complete_pending_close();
		};
		runner_.finished = [this](tasks::TaskPlan plan)
		{
			const auto keepAlive = shared_from_this();
			const auto generation = runner_.generation();
			// The host may start a new review while refreshing changed folders.
			// Its result reference must not point into that mutable view state.
			const auto completedPlan = std::move(plan);
			plan_ = completedPlan;
			running_ = false;
			cancelling_ = false;
			finished_ = true;
			// The third column becomes Status once there are results to show.
			columns_ = review_columns();
			const auto result = tasks::summarize_results(plan_);
			plan_.blockReason = optionsChangedDuringRun_
				? L"Options changed. Analyze again before running." : L"Analyze again before running.";
			const auto status = result + (optionsChangedDuringRun_ ? L" - Options changed. Analyze again." : L"");
			optionsChangedDuringRun_ = false;
			panel_.set_interactive(true);
			work_state_changed(false);
			if (const auto callback = host_.completed) callback(completedPlan);
			if (generation != runner_.generation()) return;
			set_status(status);
			if (generation != runner_.generation()) return;
			if (host_.progress) host_.progress(0, 0);
			if (generation != runner_.generation()) return;
			update_command_state();
			invalidate();
			complete_pending_close();
		};
		rebuild_controls();
		return true;
	}

	void TaskView::set_dpi(const unsigned int dpi)
	{
		dpi_ = dpi ? dpi : 96;
		panel_.set_dpi(dpi_);
		invalidate();
	}

	void TaskView::set_bounds(const recti bounds)
	{
		if (frame_) frame_->move(bounds);
	}

	void TaskView::show(const bool visible)
	{
		if (frame_) frame_->show(visible);
	}

	bool TaskView::dispose()
	{
		if (busy()) return false;
		const auto keepAlive = weak_from_this().lock();
		runner_.invalidate();
		++thumbnailGeneration_;
		const auto frame = std::exchange(frame_, {});
		if (frame)
		{
			frame->set_reactor({});
			frame->close();
		}
		return true;
	}

	void TaskView::set_targets(std::vector<std::filesystem::path> targets)
	{
		if (targets_ == targets) return;
		targets_ = std::move(targets);
		request_thumbnails();
		if (frame_ && uses_plan()) controls_changed();
		else
		{
			runner_.invalidate();
			plan_ = {};
			plan_.blockReason = L"Analyze to review the changes.";
		}
	}

	void TaskView::invalidate()
	{
		if (frame_) frame_->invalidate();
	}

	void TaskView::set_status(std::wstring text)
	{
		const auto keepAlive = weak_from_this().lock();
		statusText_ = std::move(text);
		if (host_.status) host_.status(statusText_);
		invalidate();
	}

	void TaskView::rebuild_controls()
	{
		const int focused = panel_.focused_id();
		if (panelCapture_ && frame_) frame_->release_capture();
		panel_.clear();
		build_controls();
		panel_.focus(focused);
		invalidate();
	}

	std::vector<platform::ToolbarItem> TaskView::toolbar_items()
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

		const auto weak = weak_from_this();
		runCommand_ = make(run_label(), [weak] { if (const auto self = weak.lock()) self->start_run(); }, [weak]
		{
			const auto self = weak.lock();
			return self && !self->busy() && !self->finished_ && self->plan_.can_run() &&
				self->analysis_block_reason().empty() && self->run_block_reason().empty();
		});
		auto settingsReason = analysis_block_reason();
		if (settingsReason.empty()) settingsReason = run_block_reason();
		runCommand_->tooltip = !settingsReason.empty() ? settingsReason :
			plan_.blockReason.empty() ? run_label() : plan_.blockReason;
		cancelCommand_ = make(L"Cancel", [weak] { if (const auto self = weak.lock()) self->cancel(); },
			[weak] { const auto self = weak.lock(); return self && self->busy() && !self->cancelling_; });
		refreshCommand_ = make(refresh_label(), [weak] { if (const auto self = weak.lock()) self->analyze(); },
			[weak]
			{
				const auto self = weak.lock();
				return self && !self->busy() && self->analysis_block_reason().empty();
			});
		const auto analysisReason = analysis_block_reason();
		if (!analysisReason.empty()) refreshCommand_->tooltip = analysisReason;
		std::vector<platform::ToolbarItem> items{platform::ToolbarItem::action(runCommand_)};
		items.push_back(platform::ToolbarItem::action(cancelCommand_));
		items.push_back(platform::ToolbarItem::action(refreshCommand_));
		items.push_back(platform::ToolbarItem::separator());
		append_window_commands(items);
		return items;
	}

	void TaskView::append_window_commands(std::vector<platform::ToolbarItem>& items)
	{
		const auto weak = weak_from_this();
		if (host_.toggleMaximize)
		{
			auto maximize = std::make_shared<platform::Command>();
			maximize->name = host_.maximized && host_.maximized() ? L"Restore" : L"Maximize";
			maximize->tooltip = maximize->name;
			maximize->toolbarText = [weak]
			{
				const auto self = weak.lock();
				return self && self->host_.maximized && self->host_.maximized()
					? std::wstring(L"Restore") : std::wstring(L"Maximize");
			};
			maximize->invoke = [weak]
			{
				if (const auto self = weak.lock())
				{
					if (self->host_.toggleMaximize) self->host_.toggleMaximize();
					self->refresh_commands();
				}
			};
			items.push_back(platform::ToolbarItem::action(std::move(maximize)));
		}
		closeCommand_ = std::make_shared<platform::Command>();
		closeCommand_->name = closeCommand_->tooltip = L"Close";
		closeCommand_->toolbarText = [] { return std::wstring(L"Close"); };
		closeCommand_->invoke = [weak]
		{
			if (const auto self = weak.lock()) self->ask_host_to_close();
		};
		items.push_back(platform::ToolbarItem::action(closeCommand_));
	}

	void TaskView::update_command_state()
	{
		if (runCommand_)
		{
			auto settingsReason = analysis_block_reason();
			if (settingsReason.empty()) settingsReason = run_block_reason();
			runCommand_->tooltip = !settingsReason.empty() ? settingsReason :
				plan_.blockReason.empty() ? run_label() : plan_.blockReason;
		}
		if (refreshCommand_)
		{
			const auto reason = analysis_block_reason();
			refreshCommand_->tooltip = reason.empty() ? refresh_label() : reason;
		}
		if (host_.refreshChrome) host_.refreshChrome();
	}

	void TaskView::refresh_commands()
	{
		update_command_state();
	}

	void TaskView::initialize()
	{
		if (frame_) frame_->set_focus();
		if (!uses_plan()) return;
		if (auto_analyze()) analyze();
		else
		{
			set_status(L"Analyze to review the changes.");
			update_command_state();
		}
	}

	void TaskView::analyze()
	{
		if (!uses_plan() || busy() || closePending_) return;
		const auto keepAlive = shared_from_this();
		const auto generation = runner_.invalidate();
		if (const auto reason = analysis_block_reason(); !reason.empty())
		{
			plan_ = {};
			plan_.blockReason = reason;
			finished_ = false;
			set_status(reason);
			update_command_state();
			return;
		}
		analyzing_ = true;
		cancelling_ = false;
		finished_ = false;
		plan_ = {};
		plan_.blockReason = L"Analysis is still running.";
		reviewScroll_.offset = 0;
		set_status(L"Analyzing...");
		if (runner_.generation() != generation) return;
		update_command_state();
		if (runner_.generation() != generation) return;
		bool queued = false;
		try
		{
			auto analyzer = make_analyzer();
			if (runner_.generation() != generation) return;
			if (cancelling_)
			{
				plan_.blockReason = L"Analysis cancelled. Analyze again before running.";
				const auto callback = runner_.analyzed;
				if (callback) callback(plan_);
				return;
			}
			queued = runner_.analyze(std::move(analyzer));
		}
		catch (...) {}
		if (!queued)
		{
			analyzing_ = false;
			cancelling_ = false;
			plan_.blockReason = L"Analysis could not start. Try Analyze again.";
			set_status(plan_.blockReason);
		}
		else if (cancelling_) runner_.cancel();
		update_command_state();
		invalidate();
		if (!queued) complete_pending_close();
	}

	std::wstring TaskView::analysis_summary() const
	{
		if (!plan_.can_run()) return plan_.blockReason;
		const auto collisions = tasks::describe_collisions(plan_);
		return std::format(L"{} item{}{}{}", plan_.rows.size(), plan_.rows.size() == 1 ? L"" : L"s",
			collisions.empty() ? L"" : L" - ", collisions);
	}

	void TaskView::start_run()
	{
		if (busy() || closePending_ || finished_ || !plan_.can_run() ||
			!analysis_block_reason().empty() || !run_block_reason().empty()) return;
		const auto keepAlive = shared_from_this();
		const auto generation = runner_.generation();
		running_ = true;
		cancelling_ = false;
		optionsChangedDuringRun_ = false;
		completed_ = 0;
		runningRow_ = plan_.rows.size();
		if (panelCapture_ && frame_) frame_->release_capture();
		panel_.set_interactive(false);
		work_state_changed(true);
		set_status(L"Processing...");
		update_command_state();
		bool queued = false;
		try
		{
			auto options = build_run_options();
			if (runner_.generation() != generation) return;
			if (cancelling_)
			{
				auto result = plan_;
				for (auto& row : result.rows)
					if (row.state == tasks::RowState::ready) row.state = tasks::RowState::notRun;
				const auto callback = runner_.finished;
				if (callback) callback(std::move(result));
				return;
			}
			queued = runner_.run(plan_, std::move(options));
		}
		catch (...) {}
		if (!queued)
		{
			running_ = false;
			cancelling_ = false;
			panel_.set_interactive(true);
			work_state_changed(false);
			plan_.blockReason = L"The operation could not start. Analyze again before running.";
			set_status(plan_.blockReason);
		}
		else if (cancelling_) runner_.cancel();
		update_command_state();
		invalidate();
		if (!queued) complete_pending_close();
	}

	void TaskView::cancel()
	{
		if (!busy()) return;
		cancelling_ = true;
		runner_.cancel();
		set_status(L"Cancelling...");
		update_command_state();
	}

	bool TaskView::request_close()
	{
		if (closePrompt_) return false;
		if (!busy()) return true;
		if (closePending_) return false;
		const auto keepAlive = shared_from_this();
		closePrompt_ = true;
		const bool confirmed = confirm_cancel_close();
		closePrompt_ = false;
		if (!confirmed) return false;
		if (!busy()) return true;
		closePending_ = true;
		cancel();
		return false;
	}

	bool TaskView::confirm_cancel_close()
	{
		platform::ChoiceDefinition definition;
		definition.title = title();
		definition.heading = L"This operation is still running.";
		definition.message = L"Closing now stops work that has not been done yet. Work already completed stays.";
		definition.buttons = {{1, L"Cancel operation"}, {2, L"Keep running"}};
		definition.defaultButton = 2;
		definition.warning = true;
		return platform::show_choice(frame_, definition) == 1;
	}

	void TaskView::complete_pending_close()
	{
		if (!closePending_ || busy()) return;
		closePending_ = false;
		ask_host_to_close();
	}

	void TaskView::controls_changed()
	{
		const auto keepAlive = weak_from_this().lock();
		if (running_)
		{
			optionsChangedDuringRun_ = true;
			cancel();
			return;
		}
		// Any control change discards the plan: a stale review must never reach Run.
		runner_.invalidate();
		analyzing_ = false;
		cancelling_ = false;
		plan_ = {};
		plan_.blockReason = L"Analyze to review the changes.";
		finished_ = false;
		if (closePending_) complete_pending_close();
		else if (auto_analyze()) analyze();
		else
		{
			set_status(L"Analyze to review the changes.");
			update_command_state();
			invalidate();
		}
	}

	std::filesystem::path TaskView::choose_folder(const std::wstring_view title) const
	{
		const auto chosen = platform::choose_folder(frame_, title);
		return chosen ? *chosen : std::filesystem::path{};
	}

	void TaskView::show_choice_popup(ui::Control& control)
	{
		if (control.choices.empty() || !frame_) return;
		std::vector<platform::CommandPtr> commands;
		std::vector<platform::PopupItem> items;
		commands.reserve(control.choices.size());
		items.reserve(control.choices.size());
		for (size_t index = 0; index < control.choices.size(); ++index)
		{
			auto command = std::make_shared<platform::Command>();
			command->name = control.choices[index];
			const int chosen = static_cast<int>(index);
			command->invoke = [this, id = control.id, chosen]
			{
				auto* target = panel_.find(id);
				if (!target || !target->enabled || !panel_.interactive() || target->value == chosen) return;
				target->value = chosen;
				const auto changed = target->changed;
				invalidate();
				if (changed) changed();
			};
			command->checked = [this, id = control.id, chosen]
			{
				const auto* target = panel_.find(id);
				return target && target->value == chosen;
			};
			commands.push_back(command);
			items.push_back(platform::PopupItem::action(command, command->name));
		}
		const auto origin = frame_->client_to_screen({control.hit.x, control.hit.bottom()});
		platform::show_popup_menu(frame_, origin, items);
	}

	void TaskView::control_activated(ui::Control& control)
	{
		switch (control.kind)
		{
		case ui::ControlKind::choice:
			show_choice_popup(control);
			break;
		case ui::ControlKind::folder:
		{
			const int id = control.id;
			const auto label = control.label;
			if (const auto chosen = choose_folder(label); !chosen.empty())
			{
				auto* target = panel_.find(id);
				if (!target || !target->enabled || target->text == chosen.wstring()) break;
				target->text = chosen.wstring();
				const auto changed = target->changed;
				invalidate();
				if (changed) changed();
			}
			break;
		}
		default:
			const auto changed = control.changed;
			if (changed) changed();
			break;
		}
	}

	std::wstring TaskView::review_cell(const tasks::TaskRow& row, const size_t column) const
	{
		switch (column)
		{
		case 0: return row.source.filename().wstring();
		case 1: return row.destination.filename().wstring();
		default:
			if (finished_ || running_)
			{
				const std::wstring status = state_text(row.state);
				const auto& outcome = row.detail.empty() ? row.change : row.detail;
				return outcome.empty() ? status : status + L" - " + outcome;
			}
			return row.change;
		}
	}

	int TaskView::row_height() const
	{
		return metric(20);
	}

	void TaskView::layout(const ui::CanvasRenderer& renderer)
	{
		if (!frame_) return;
		const recti client = frame_->client_rect();
		const int panel = (std::min)(metric(panelWidth), (std::max)(metric(160), client.width / 2));
		const int gap = metric(8);
		const int statusHeight = metric(24);
		const recti panelArea{gap, gap, panel - gap * 2, client.height - gap * 2};
		// The panel is laid out at its scrolled position, so hit testing and drawing agree.
		panelBounds_ = {
			panelArea.x, panelArea.y - panelScroll_.offset,
			(std::max)(0, panelArea.width - metric(12)), panelArea.height
		};
		panel_.layout(panelBounds_, renderer);

		const int thumbnailRows = targets_.empty()
			                          ? 0
			                          : metric(thumbnailEdge) + renderer.measure_text(L"Wg").height + gap;
		const auto previousOffset = panelScroll_.offset;
		panelScroll_.layout(panelArea, panel_.content_height() + thumbnailRows + gap, panelScroll_.offset,
		                    metric(12), metric(28));
		if (previousOffset != panelScroll_.offset)
		{
			panelBounds_.y = panelArea.y - panelScroll_.offset;
			panel_.layout(panelBounds_, renderer);
		}
		const int thumbnailTop = panelBounds_.y + panel_.content_height() + gap;
		thumbnailBounds_ = {panelBounds_.x, thumbnailTop, panelBounds_.width, thumbnailRows};

		const recti right{panel, gap, (std::max)(0, client.width - panel - gap), client.height - gap * 2};
		reviewHeaderBounds_ = {right.x, right.y, right.width, metric(22)};
		statusBounds_ = {right.x, right.bottom() - statusHeight, right.width, statusHeight};
		reviewBounds_ = {
			right.x, reviewHeaderBounds_.bottom(), right.width,
			(std::max)(0, statusBounds_.y - reviewHeaderBounds_.bottom() - metric(4))
		};
		contentBounds_ = {
			right.x, right.y, right.width, (std::max)(0, statusBounds_.y - right.y - metric(4))
		};
		reviewScroll_.layout(reviewBounds_, static_cast<int>(plan_.rows.size()) * row_height(),
		                     reviewScroll_.offset, metric(12), metric(28));
		layout_hosted_controls();
	}

	void TaskView::ensure_row_visible(const size_t row)
	{
		const int height = row_height();
		const int top = static_cast<int>(row) * height;
		const int viewport = reviewScroll_.viewportExtent;
		if (viewport <= 0) return;
		if (top < reviewScroll_.offset) reviewScroll_.offset = top;
		else if (top + height > reviewScroll_.offset + viewport)
			reviewScroll_.offset = (std::min)(reviewScroll_.maximum(), top + height - viewport);
		invalidate();
	}

	void TaskView::draw_panel(const ui::CanvasRenderer& renderer) const
	{
		const recti clip{
			panelBounds_.x, metric(8), panelBounds_.width + metric(12),
			panelScroll_.viewportExtent > 0 ? panelScroll_.viewportExtent : panelBounds_.height
		};
		renderer.clip(clip);
		panel_.draw(renderer);
		if (!targets_.empty() && thumbnailBounds_.height > 0)
		{
			const int edge = metric(thumbnailEdge);
			const int spacing = metric(4);
			int x = thumbnailBounds_.x;
			for (const auto& thumbnail : thumbnails_)
			{
				if (x + edge > thumbnailBounds_.right()) break;
				const recti cell{x, thumbnailBounds_.y, edge, edge};
				if (thumbnail.width > 0 && thumbnail.height > 0)
				{
					const recti fitted = ui::fit_centered({thumbnail.width, thumbnail.height}, cell);
					renderer.image({thumbnail.width, thumbnail.height, thumbnail.pixels, false, 0}, fitted);
				}
				else
				{
					renderer.fill(cell, platform::system_color(platform::SystemColor::face));
					renderer.outline(cell, platform::system_color(platform::SystemColor::shadow));
				}
				x += edge + spacing;
			}
			renderer.text(std::format(L"{} item{} selected", targets_.size(),
			                          targets_.size() == 1 ? L"" : L"s"), {
				              thumbnailBounds_.x, thumbnailBounds_.y + edge + spacing, thumbnailBounds_.width,
				              thumbnailBounds_.height - edge - spacing
			              }, platform::system_color(platform::SystemColor::grayText),
			              platform::TextFormat::left | platform::TextFormat::singleLine);
		}
		renderer.reset_clip();
		renderer.scrollbar(panelScroll_);
	}

	void TaskView::draw_review(const ui::CanvasRenderer& renderer) const
	{
		const color window = platform::system_color(platform::SystemColor::window);
		const color face = platform::system_color(platform::SystemColor::face);
		const color shadow = platform::system_color(platform::SystemColor::shadow);
		const color text = platform::system_color(platform::SystemColor::windowText);
		const color gray = platform::system_color(platform::SystemColor::grayText);

		renderer.fill(reviewHeaderBounds_, face);
		renderer.outline(reviewHeaderBounds_, shadow);
		const recti viewport = reviewScroll_.viewport(reviewBounds_);
		renderer.fill(viewport, window);

		int totalWeight = 0;
		for (const auto& column : columns_) totalWeight += (std::max)(1, column.weight);
		if (!totalWeight) return;

		const auto columnRect = [&](const recti row, const size_t index)
		{
			int offset = 0;
			for (size_t scan = 0; scan < index; ++scan)
				offset += row.width * (std::max)(1, columns_[scan].weight) / totalWeight;
			const int width = row.width * (std::max)(1, columns_[index].weight) / totalWeight;
			return recti{row.x + offset + metric(4), row.y, (std::max)(0, width - metric(8)), row.height};
		};

		for (size_t index = 0; index < columns_.size(); ++index)
			renderer.text(columns_[index].title, columnRect(reviewHeaderBounds_, index), text,
			              platform::TextFormat::left | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine);

		if (plan_.rows.empty())
		{
			renderer.text(analyzing_ ? L"Analyzing..." : L"Nothing to review yet.", viewport, gray,
			              platform::TextFormat::center | platform::TextFormat::verticalCenter |
			              platform::TextFormat::singleLine);
			renderer.scrollbar(reviewScroll_);
			return;
		}

		const int height = row_height();
		const size_t first = static_cast<size_t>((std::max)(0, reviewScroll_.offset) / height);
		const size_t last = (std::min)(plan_.rows.size(),
		                               first + static_cast<size_t>(viewport.height / height) + 2);
		renderer.clip(viewport);
		for (size_t index = first; index < last; ++index)
		{
			const auto& row = plan_.rows[index];
			const recti bounds{
				viewport.x, viewport.y + static_cast<int>(index) * height - reviewScroll_.offset,
				viewport.width, height
			};
			if (running_ && index == runningRow_)
				renderer.fill(bounds, platform::system_color(platform::SystemColor::highlight));
			else if (index % 2) renderer.fill(bounds, face);
			const bool selectedRow = running_ && index == runningRow_;
			const color foreground = selectedRow
				                         ? platform::system_color(platform::SystemColor::highlightText)
				                         : row.state == tasks::RowState::blocked ||
				                         row.state == tasks::RowState::failed
				                         ? color{0xb0, 0x30, 0x20, 0xff}
				                         : row.state == tasks::RowState::skipped ||
				                         row.state == tasks::RowState::notRun
				                         ? gray
				                         : text;
			for (size_t column = 0; column < columns_.size(); ++column)
				renderer.text(review_cell(row, column), columnRect(bounds, column), foreground,
				              platform::TextFormat::left | platform::TextFormat::verticalCenter |
				              platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
		}
		renderer.reset_clip();
		renderer.scrollbar(reviewScroll_);
	}

	void TaskView::draw_status(const ui::CanvasRenderer& renderer) const
	{
		auto settingsReason = !busy() && !finished_ ? analysis_block_reason() : std::wstring{};
		if (settingsReason.empty() && !busy() && !finished_) settingsReason = run_block_reason();
		const auto& text = settingsReason.empty() ? statusText_ : settingsReason;
		renderer.text(text, statusBounds_,
		              settingsReason.empty() && (plan_.can_run() || finished_ || text.empty())
			              ? platform::system_color(platform::SystemColor::windowText)
			              : color{0xb0, 0x30, 0x20, 0xff},
		              platform::TextFormat::left | platform::TextFormat::verticalCenter |
		              platform::TextFormat::singleLine | platform::TextFormat::endEllipsis);
	}

	void TaskView::draw_content(const ui::CanvasRenderer& renderer, recti)
	{
		draw_review(renderer);
	}

	bool TaskView::content_mouse(const platform::MouseMessage message, const platform::MouseInput& input)
	{
		if (message != platform::MouseMessage::wheel) return false;
		if (reviewScroll_.scroll_by(-input.wheelDelta / 2)) invalidate();
		return true;
	}

	bool TaskView::content_key(const platform::KeyInput& input)
	{
		switch (input.key)
		{
		case platform::KeyCode::down: reviewScroll_.scroll_by(row_height());
			break;
		case platform::KeyCode::up: reviewScroll_.scroll_by(-row_height());
			break;
		case platform::KeyCode::pageDown: reviewScroll_.scroll_by(reviewScroll_.viewportExtent);
			break;
		case platform::KeyCode::pageUp: reviewScroll_.scroll_by(-reviewScroll_.viewportExtent);
			break;
		case platform::KeyCode::home: reviewScroll_.offset = 0;
			break;
		case platform::KeyCode::end: reviewScroll_.offset = reviewScroll_.maximum();
			break;
		default: return false;
		}
		invalidate();
		return true;
	}

	void TaskView::paint(const platform::WindowFramePtr& frame, platform::DrawContext& draw)
	{
		if (frame != frame_) return;
		const ui::CanvasRenderer renderer(draw, platform::create_message_font(dpi_));
		const recti client = frame_->client_rect();
		renderer.fill(client, platform::system_color(platform::SystemColor::window));
		layout(renderer);
		draw_panel(renderer);
		draw_content(renderer, contentBounds_);
		draw_status(renderer);
	}

	void TaskView::size(const platform::WindowFramePtr& frame, sizei, platform::MeasureContext&)
	{
		if (frame == frame_) invalidate();
	}

	platform::MessageResult TaskView::message(const platform::WindowFramePtr& frame,
	                                          const platform::WindowMessage message)
	{
		if (frame == frame_ && message == platform::WindowMessage::destroy)
		{
			const auto keepAlive = shared_from_this();
			runner_.invalidate();
			++thumbnailGeneration_;
			analyzing_ = running_ = cancelling_ = closePending_ = false;
			frame_.reset();
			frame->set_reactor({});
			return 0;
		}
		if (frame == frame_ && message == platform::WindowMessage::captureLost)
		{
			panel_.mouse_up({});
			panelCapture_ = false;
			draggedScroll_ = nullptr;
			content_mouse(platform::MouseMessage::leave, {});
		}
		return 0;
	}

	platform::MessageResult TaskView::mouse(const platform::WindowFramePtr& frame,
	                                        const platform::MouseMessage message,
	                                        const platform::MouseInput& input)
	{
		if (frame != frame_) return 0;
		const auto keepAlive = shared_from_this();
		if (content_dragging() && (message == platform::MouseMessage::move ||
			message == platform::MouseMessage::leftButtonUp))
			return content_mouse(message, input) ? 1 : 0;
		const bool insidePanel = input.point.x >= panelBounds_.x &&
			input.point.x < panelBounds_.right() && input.point.y >= metric(8) &&
			input.point.y < metric(8) + panelScroll_.viewportExtent;
		switch (message)
		{
		case platform::MouseMessage::move:
			if (!trackingMouse_)
			{
				trackingMouse_ = true;
				frame_->track_mouse_leave();
			}
			if (draggedScroll_)
			{
				draggedScroll_->set_thumb_position(input.point.y - scrollGrab_);
				invalidate();
			}
			else if (panel_.dragging() || insidePanel) panel_.mouse_move(input.point);
			else
			{
				panel_.mouse_leave();
				content_mouse(message, input);
			}
			break;

		case platform::MouseMessage::leave:
			trackingMouse_ = false;
			panel_.mouse_leave();
			break;

		case platform::MouseMessage::leftButtonDown:
			frame_->set_focus();
			for (auto* scroll : {&panelScroll_, &reviewScroll_})
			{
				if (!scroll->visible() || !scroll->track.contains(input.point)) continue;
				if (scroll->thumb.contains(input.point))
				{
					draggedScroll_ = scroll;
					scrollGrab_ = input.point.y - scroll->thumb.y;
					frame_->set_capture();
				}
				else scroll->scroll_by(input.point.y < scroll->thumb.y
					? -scroll->viewportExtent : scroll->viewportExtent);
				invalidate();
				return 1;
			}
			if (insidePanel && panel_.mouse_down(input.point))
			{
				if (panel_.dragging())
				{
					panelCapture_ = true;
					frame_->set_capture();
				}
				break;
			}
			panel_.focus(0);
			content_mouse(message, input);
			break;

		case platform::MouseMessage::leftButtonUp:
		{
			const bool captured = panelCapture_ || draggedScroll_;
			const bool handled = panel_.mouse_up(input.point) || panelCapture_ || draggedScroll_;
			panelCapture_ = false;
			draggedScroll_ = nullptr;
			if (captured) frame_->release_capture();
			if (!handled) content_mouse(message, input);
			break;
		}

		case platform::MouseMessage::wheel:
			// The pointer decides which of the two scrolling regions moves.
			if (input.point.x < contentBounds_.x)
			{
				if (panelScroll_.scroll_by(-input.wheelDelta / 2)) invalidate();
				break;
			}
			content_mouse(message, input);
			break;

		default:
			content_mouse(message, input);
			break;
		}
		return 0;
	}

	void TaskView::bind_text_input(const int controlId, platform::TextInputOptions& options)
	{
		const auto weak = weak_from_this();
		options.escaped = [weak]
		{
			if (const auto self = weak.lock()) self->ask_host_to_close();
		};
		options.tabbed = [weak, controlId](const bool backwards)
		{
			const auto self = weak.lock();
			if (!self) return;
			self->panel_.focus(controlId);
			platform::KeyInput input;
			input.character = L'\t';
			input.shift = backwards;
			self->control_key(input);
		};
	}

	bool TaskView::control_key(const platform::KeyInput& input)
	{
		if (input.character == L'\t' && !input.control && !input.alt)
		{
			if (frame_) frame_->set_focus();
			panel_.focus_next(input.shift);
			if (auto* control = panel_.find(panel_.focused_id()))
			{
				const int top = (std::max)(0, control->bounds.y - metric(8) + panelScroll_.offset);
				if (top < panelScroll_.offset) panelScroll_.offset = top;
				else if (top + control->bounds.height > panelScroll_.offset + panelScroll_.viewportExtent)
					panelScroll_.offset = (std::min)(panelScroll_.maximum(),
						top + control->bounds.height - panelScroll_.viewportExtent);
				if (control->kind == ui::ControlKind::text) control_activated(*control);
			}
			invalidate();
			return true;
		}
		return panel_.key(input);
	}

	platform::MessageResult TaskView::key(const platform::WindowFramePtr& frame,
	                                      const platform::KeyMessage message, const platform::KeyInput& input)
	{
		if (frame != frame_) return 0;
		const auto keepAlive = shared_from_this();
		if (message == platform::KeyMessage::character)
			return input.character == L'\t' && control_key(input) ? 1 : 0;
		if (message != platform::KeyMessage::down) return 0;
		if (input.key == platform::KeyCode::escape)
		{
			if (content_dragging()) return content_key(input) ? 1 : 0;
			if (host_.closed) host_.closed();
			return 1;
		}
		return control_key(input) || content_key(input) ? 1 : 0;
	}

	void TaskView::request_thumbnails()
	{
		thumbnails_.assign((std::min)(targets_.size(), maximumThumbnails), {});
		const auto generation = ++thumbnailGeneration_;
		const int edge = metric(thumbnailEdge);
		const auto weak = weak_from_this();
		for (size_t index = 0; index < thumbnails_.size(); ++index)
		{
			platform::queue_work(platform::WorkQueue::thumbnail,
			                     [weak, generation, index, edge, path = targets_[index]]
			                     {
				                     auto decoded = files::load_thumbnail(path, edge, edge);
				                     platform::queue_ui([weak, generation, index, decoded = std::move(decoded)]
				                     {
					                     const auto self = weak.lock();
					                     if (!self || self->thumbnailGeneration_ != generation) return;
					                     if (index >= self->thumbnails_.size()) return;
					                     self->thumbnails_[index] = {
						                     decoded.width, decoded.height, std::move(decoded.pixels)
					                     };
					                     self->invalidate();
				                     });
			                     });
		}
		invalidate();
	}
}
