// ImageWalker by Zac Walker
// Selects test or interactive startup through libdesktop's ui application lifecycle.

#include "Platform.h"
#include "MainWindow.h"
#include "Tests.h"
#include "DiagnosticLog.h"
#include <iw/imaging/Diagnostics.h>
#include "Undo.h"
#include "PlatformWin32.h"
#include "window_screenshot.h"
#include "Appearance.h"
#include "AgentSession.h"

#include <filesystem>
#include <cctype>
#include <thread>

namespace
{
	std::vector<std::string> split_command_line(const std::string_view text)
	{
		std::vector<std::string> result;
		std::string current;
		bool quoted = false;
		for (size_t i = 0; i < text.size(); ++i)
		{
			const char c = text[i];
			if (c == '"')
			{
				quoted = !quoted;
				continue;
			}
			if (!quoted && (c == ' ' || c == '\t'))
			{
				if (!current.empty())
				{
					result.push_back(std::move(current));
					current.clear();
				}
				continue;
			}
			current.push_back(c);
		}
		if (!current.empty()) result.push_back(std::move(current));
		return result;
	}

	bool switch_is(std::string_view value, std::string_view name)
	{
		while (value.size() > 1 && (value.front() == '/' || value.front() == '-')) value.remove_prefix(1);
		if (value.size() != name.size()) return false;
		for (size_t i = 0; i < value.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(value[i])) != std::tolower(static_cast<unsigned char>(name[i])))
				return false;
		return true;
	}
}

namespace iw
{
	class ImageWalkerApp final : public ::ui::app, public std::enable_shared_from_this<ImageWalkerApp>
	{
	public:
		explicit ImageWalkerApp(const screenshot::Request& screenshot, std::unique_ptr<AgentSession> agent = {}) :
			initialPath_(screenshot.input), capture_(screenshot), agent_(std::move(agent)),
			screenshotMode_(screenshot.enabled)
		{
		}

		int result(int code) const { return agent_ && !agentStarted_ && code == 0 ? 1 : capture_.result(code); }

		bool pre_init() override
		{
			if (agent_) return true;
			diagnostic_session_ = std::make_unique<diagnostics::Session>();
			imaging::set_diagnostic_sink([](std::wstring_view message) { iw::platform::write_diagnostic(message); });
			return true;
		}

		bool parse_command_line(std::string_view commandLine) override
		{
			if (screenshotMode_ || agent_) return false;
			const auto arguments = split_command_line(commandLine);
			for (const auto& argument : arguments)
			{
				if (switch_is(argument, "test"))
				{
					runTests_ = true;
				}
				else if (argument == "/test-no-clipboard" || argument == "--test-no-clipboard" ||
				         argument == "-test-no-clipboard")
				{
					runTests_ = true;
					skipClipboardTests_ = true;
				}
				else if (initialPath_.empty() && !argument.empty() && argument.front() != '/' &&
				         argument.front() != '-')
					initialPath_ = std::filesystem::path(::core::utf8_to_utf16(argument));
			}
			return runTests_;
		}

		int run_console_mode() override
		{
			// platform::run has already initialized the UI apartment.  The
			// old standalone entry point needed its own MTA Runtime; re-entering COM
			// here would report RPC_E_CHANGED_MODE and skip the suite.
			if (!runTests_) return 0;
			int result = 1;
			std::thread tests([this, &result]
			{
				const iw::platform::Runtime runtime(iw::platform::RuntimeMode::multithreaded);
				result = runtime ? tests::run_all(skipClipboardTests_) : 1;
			});
			tests.join();
			diagnostic_session_.reset();
			return result;
		}

		bool load_settings(const ::platform::setting_file_ptr&) override { return true; }
		::core::mcp::server_ptr agent_server() override { return agent_ ? agent_->server() : nullptr; }

		bool can_exit() override { return agent_ || !main_window_ || main_window_->can_exit(); }

		bool init(std::string_view) override
		{
			platform::win32::refresh_appearance();
			::platform::setting_file_ptr store;
			if (agent_) main_window_ = std::make_shared<MainWindow>();
			else
			{
				iw::undo::initialize_live_history();
				main_window_ = std::make_shared<MainWindow>(&iw::undo::history());
				auto folder = ::platform::running_app_path().folder();
				if (folder.is_empty()) folder = ::platform::current_directory();
				store = ::platform::create_ini_file_settings(folder);
			}
			auto host = iw::platform::create_frame_host(agent_ ? nullptr : main_window_);
			::ui::frame_style style;
			style.drawn_caption = false;
			style.client_size = {1280, 800};
			style.minimum_client = {640, 480};
			style.resizable = true;
			style.can_drop = !agent_;
			style.hardware_accelerated = !agent_;
			style.offscreen = agent_ != nullptr;
			style.no_focus = agent_ != nullptr;
			style.timer_milliseconds = 50;
			style.colors = platform::frame_colors();
			const auto frame = ::ui::create_app_frame(shared_from_this(), store, host, style);
			if (!frame)
			{
				if (agent_) platform::agent::report_error("Could not create the fixture frame.");
				iw::platform::write_diagnostic(L"ImageWalker 3.0 could not create its main frame.\n");
				return false;
			}
			if (agent_ && !platform::agent::secure_window(frame->handle()))
			{
				frame->destroy();
				platform::agent::report_error("A cloaked, non-interactive window is not available on this desktop.");
				return false;
			}
			iw::platform::bind_main_frame(frame, host);
			if (agent_)
			{
				agentStarted_ = agent_->start(main_window_);
				if (!agentStarted_) platform::agent::report_error("Could not initialize the isolated fixture window.");
				return agentStarted_;
			}
			if (diagnostic_session_ && diagnostic_session_->error())
			{
				iw::platform::write_diagnostic(L"Unable to create the ImageWalker diagnostic log.\n");
				iw::platform::show_error(L"The diagnostic log could not be created beside the executable. Check that "
				                         L"the application folder is writable.",
				                         L"ImageWalker 3.0 logging");
			}
			if (!main_window_->create(screenshotMode_ ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL, initialPath_))
			{
				iw::platform::show_error(L"ImageWalker 3.0 could not create its main window.", L"ImageWalker 3.0");
				return false;
			}
			platform::win32::refresh_appearance(platform::win32::owner_window(main_window_->frame()));
			if (!capture_.start(platform::win32::owner_window(main_window_->frame())))
			{
				screenshot::report(L"could not start the capture timer.");
				return false;
			}
			return true;
		}

		bool wants_gpu() const override { return !agent_; }

		void system_event(const ::platform::os_event_type event) override
		{
			if (event == ::platform::os_event_type::graphics_device_lost) iw::platform::release_graphics();
			if (event == ::platform::os_event_type::options_changed && main_window_)
				platform::win32::refresh_appearance(platform::win32::owner_window(main_window_->frame()));
		}

		bool key_down(char32_t key, ::platform::key_state keys) override
		{
			if (agent_) return false;
			if (key == ::platform::keys::ESCAPE && !keys.control && !keys.alt && !keys.shift && main_window_ &&
			    main_window_->escape_active_view())
				return true;
			if (main_window_ && main_window_->view_shortcut(key, keys.control, keys.shift, keys.alt)) return true;
			return iw::platform::dispatch_menu_accelerator(key, keys);
		}

		bool text_input(std::string_view) override { return false; }

		::ui::focus_mode focus_mode() const override { return ::ui::focus_mode::view; }

		void track_menu(const ::ui::frame_ptr& parent, core::recti bounds,
		                const std::vector<::ui::command_ptr>& commands) override
		{
			if (parent && !agent_) parent->track_menu(bounds, commands);
		}

		void final_exit() override
		{
			iw::platform::shutdown_runtime();
			if (agent_) agent_->stop();
			if (main_window_ && main_window_->frame()) main_window_->frame()->set_reactor({});
			main_window_.reset();
			diagnostic_session_.reset();
		}

	private:
		std::shared_ptr<MainWindow> main_window_;
		std::unique_ptr<diagnostics::Session> diagnostic_session_;
		std::filesystem::path initialPath_;
		screenshot::Timer capture_;
		std::unique_ptr<AgentSession> agent_;
		bool agentStarted_{};
		bool screenshotMode_{};
		bool runTests_{};
		bool skipClipboardTests_{};
	};
}

int main(int argc, char* argv[])
{
	const auto request = iw::platform::agent::command_line();
	if (request.enabled)
	{
		if (!request.error.empty())
		{
			iw::platform::agent::report_error(request.error);
			return 2;
		}
		std::string error;
		auto fixture = iw::platform::agent::Fixture::open(request.root, error);
		if (!fixture)
		{
			iw::platform::agent::report_error(error);
			return 2;
		}
		// libdesktop::run opens its default INI and log before any app hook can bypass them.
		// Give even those runtime files a different identity from the interactive application.
		::core::set_app_name("imagewalker30-mcp");
		const iw::screenshot::Request noTimedCapture;
		auto agent = std::make_unique<iw::AgentSession>(std::move(fixture), request.capture);
		const auto app = std::make_shared<iw::ImageWalkerApp>(noTimedCapture, std::move(agent));
		return app->result(::platform::run(app, argc, argv));
	}
	::core::set_app_name("imagewalker30");
	const auto screenshot = iw::screenshot::command_line();
	if (screenshot.enabled && screenshot.output.empty())
	{
		iw::screenshot::report_usage();
		return 2;
	}
	auto app = std::make_shared<iw::ImageWalkerApp>(screenshot);
	return app->result(::platform::run(app, argc, argv));
}
