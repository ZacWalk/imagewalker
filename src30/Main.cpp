// ImageWalker by Zac Walker
// Selects test or interactive application startup from platform-supplied arguments.

#include "Platform.h"
#include "MainWindow.h"
#include "Tests.h"
#include "DiagnosticLog.h"
#include "Undo.h"

#include <filesystem>

namespace
{
	bool is_switch(const wchar_t* argument, const wchar_t* name)
	{
		return argument && (argument[0] == L'/' || argument[0] == L'-') &&
			iw::platform::compare_ordinal_ignore_case(argument + 1, name) == 0;
	}
}

int imagewalker_main(const int showCommand, const std::span<const std::wstring_view> arguments)
{
	std::filesystem::path initialPath;
	bool runTests = false;
	for (const auto argument : arguments)
	{
		if (is_switch(argument.data(), L"test")) runTests = true;
		else if (initialPath.empty() && !argument.empty() && argument.front() != L'/' && argument.front() != L'-')
			initialPath = argument;
	}

	const iw::diagnostics::Session log;
	if (log.error())
	{
		iw::platform::write_diagnostic(L"Unable to create the ImageWalker diagnostic log.\n");
		if (!runTests)
			iw::platform::show_error(L"The diagnostic log could not be created beside the executable. "
				L"Check that the application folder is writable.", L"ImageWalker 3.0 logging");
	}

	if (runTests)
	{
		const iw::platform::Runtime runtime(iw::platform::RuntimeMode::multithreaded);
		return runtime ? iw::tests::run_all() : 1;
	}

	const iw::platform::Runtime runtime(iw::platform::RuntimeMode::userInterface);
	if (!runtime) return 1;

	iw::undo::initialize_live_history();
	auto mainWindow = std::make_shared<iw::MainWindow>(&iw::undo::history());
	if (!mainWindow->create(showCommand, initialPath))
	{
		iw::platform::show_error(L"ImageWalker 3.0 could not create its main window.", L"ImageWalker 3.0");
		return 1;
	}
	return iw::platform::run_ui_loop(mainWindow->frame());
}
