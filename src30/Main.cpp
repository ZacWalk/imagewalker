// ImageWalker by Zac Walker
// Selects test or interactive startup through libdesktop's ui application lifecycle.

#include "Platform.h"
#include "MainWindow.h"
#include "Tests.h"
#include "DiagnosticLog.h"
#include <iw/imaging/Diagnostics.h>
#include "Undo.h"

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
		bool pre_init() override
		{
			diagnostic_session_ = std::make_unique<diagnostics::Session>();
			imaging::set_diagnostic_sink([](std::wstring_view message)
			{
				iw::platform::write_diagnostic(message);
			});
			::render::decode_embedded_image = [](const core::cspan data) -> core::surface_ptr
			{
				return ::platform::load_image_memory(std::span<const std::uint8_t>(data.data, data.size));
			};
			return true;
		}

		bool parse_command_line(std::string_view commandLine) override
		{
			const auto arguments = split_command_line(commandLine);
			size_t start = !arguments.empty() && arguments.front().find(".exe") != std::string::npos ? 1 : 0;
			for (size_t i = start; i < arguments.size(); ++i)
			{
				const auto& argument = arguments[i];
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

		bool can_exit() override { return !main_window_ || main_window_->can_exit(); }

		bool init(std::string_view) override
		{
			iw::undo::initialize_live_history();
			main_window_ = std::make_shared<MainWindow>(&iw::undo::history());
			auto host = iw::platform::create_frame_host(main_window_);
			auto folder = ::platform::running_app_path().folder();
			if (folder.is_empty()) folder = ::platform::current_directory();
			auto store = ::platform::create_ini_file_settings(folder);
			::ui::frame_style style;
			style.drawn_caption = false;
			style.client_size = {1280, 800};
			style.minimum_client = {640, 480};
			style.resizable = true;
			style.can_drop = true;
			style.hardware_accelerated = true;
			style.timer_milliseconds = 50;
			style.colors = {::ui::style::colors.view_background, ::ui::style::colors.view_text,
				::ui::style::colors.view_selected_background};
			const auto frame = ::ui::create_app_frame(shared_from_this(), store, host, style);
			if (!frame)
			{
				iw::platform::write_diagnostic(L"ImageWalker 3.0 could not create its main frame.\n");
				return false;
			}
			iw::platform::bind_main_frame(frame, host);
			if (diagnostic_session_ && diagnostic_session_->error())
			{
				iw::platform::write_diagnostic(L"Unable to create the ImageWalker diagnostic log.\n");
				iw::platform::show_error(L"The diagnostic log could not be created beside the executable. Check that "
				                         L"the application folder is writable.",
					L"ImageWalker 3.0 logging");
			}
			if (!main_window_->create(1, initialPath_))
			{
				iw::platform::show_error(L"ImageWalker 3.0 could not create its main window.", L"ImageWalker 3.0");
				return false;
			}
			return true;
		}

		bool wants_gpu() const override { return true; }

		bool key_down(char32_t key, ::platform::key_state keys) override
		{
			return iw::platform::dispatch_menu_accelerator(key, keys);
		}

		bool text_input(std::string_view) override { return false; }

		::ui::focus_mode focus_mode() const override { return ::ui::focus_mode::view; }

		void track_menu(
			const ::ui::frame_ptr& parent, core::recti bounds, const std::vector<::ui::command_ptr>& commands) override
		{
			if (parent) parent->track_menu(bounds, commands);
		}

		void final_exit() override
		{
			iw::platform::shutdown_runtime();
			if (main_window_ && main_window_->frame()) main_window_->frame()->set_reactor({});
			main_window_.reset();
			diagnostic_session_.reset();
		}

	private:
		std::shared_ptr<MainWindow> main_window_;
		std::unique_ptr<diagnostics::Session> diagnostic_session_;
		std::filesystem::path initialPath_;
		bool runTests_{};
		bool skipClipboardTests_{};
	};
}

int main(int argc, char* argv[])
{
	::core::set_app_name("imagewalker30");
	auto app = std::make_shared<iw::ImageWalkerApp>();
	return ::platform::run(app, argc, argv);
}
