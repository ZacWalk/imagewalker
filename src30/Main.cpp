// ImageWalker by Zac Walker
// Selects test or interactive application startup from platform-supplied arguments.

#include "Platform.h"
#include "MainWindow.h"
#include "Tests.h"
#include "DiagnosticLog.h"
#include <iw/imaging/Diagnostics.h>
#include "Undo.h"

#include <filesystem>
#include <thread>

const std::string_view s_app_name = "ImageWalker30";

namespace
{
	std::shared_ptr<iw::MainWindow> main_window;
	std::unique_ptr<iw::diagnostics::Session> diagnostic_session;
}

app_init_result app_init(const pf::window_frame_ptr& mainFrame,
                         const std::span<const std::string_view> arguments)
{
	pf::config_set_app_name("imagewalker30");
	std::filesystem::path initialPath;
	bool runTests = false;
	bool skipClipboardTests = false;
	for (const auto argument : arguments)
	{
		if (argument == "/test" || argument == "--test" || argument == "-test" ||
			(argument.size() > 1 && (argument.front() == '/' || argument.front() == '-') &&
				pf::icmp(argument.substr(1), "test") == 0))
			runTests = true;
		else if (argument == "/test-no-clipboard" || argument == "--test-no-clipboard" ||
		         argument == "-test-no-clipboard")
		{
			runTests = true;
			skipClipboardTests = true;
		}
		else if (initialPath.empty() && !argument.empty() && argument.front() != '/' && argument.front() != '-')
			initialPath = std::filesystem::path(pf::utf8_to_utf16(argument));
	}

	diagnostic_session = std::make_unique<iw::diagnostics::Session>();
	iw::imaging::set_diagnostic_sink([](std::wstring_view message)
	{
		iw::platform::write_diagnostic(message);
	});
	if (diagnostic_session->error())
	{
		iw::platform::write_diagnostic(L"Unable to create the ImageWalker diagnostic log.\n");
		if (!runTests)
			iw::platform::show_error(L"The diagnostic log could not be created beside the executable. "
				L"Check that the application folder is writable.", L"ImageWalker 3.0 logging");
	}

	if (runTests)
	{
		// libdesktop's WinMain has already initialized the UI apartment.  The
		// old standalone entry point needed its own MTA Runtime; re-entering COM
		// here would report RPC_E_CHANGED_MODE and skip the suite.
		int result = 1;
		std::thread tests([&result, skipClipboardTests]
		{
			const iw::platform::Runtime runtime(iw::platform::RuntimeMode::multithreaded);
			result = runtime ? iw::tests::run_all(skipClipboardTests) : 1;
		});
		tests.join();
		diagnostic_session.reset();
		return {false, result};
	}

	iw::platform::bind_main_frame(mainFrame);
	iw::undo::initialize_live_history();
	main_window = std::make_shared<iw::MainWindow>(&iw::undo::history());
	if (!main_window->create(1, initialPath))
	{
		app_init_result failure;
		failure.start_gui = false;
		failure.exit_code = 1;
		failure.error_message = "ImageWalker 3.0 could not create its main window.";
		return failure;
	}

	app_init_result result;
	result.window.initial_client_size = {1280, 800};
	result.window.minimum_client_size = {640, 480};
	result.window.resizable = true;
	return result;
}

void app_idle()
{
}

void app_destroy()
{
	iw::platform::shutdown_runtime();
	main_window.reset();
	diagnostic_session.reset();
}
