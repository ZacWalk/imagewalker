#pragma once

#include <new>

namespace
{
	bool WriteWorkflowPng(const CString &path, COLORREF color)
	{
		IW::CFile file;
		CLoadPng encoder;
		return file.OpenForWrite(path) && encoder.Write(_T("PNG"), &file, WorkflowSolidImage(color),
			IW::CodecSettings{}, IW::CNullStatus::Instance);
	}

	struct WorkflowRenameDriver
	{
		inline static WorkflowRenameDriver *active = nullptr;
		UINT_PTR timer = 0;
		bool accept;
		bool visited = false;
		bool ready = false;

		explicit WorkflowRenameDriver(bool acceptIn) : accept(acceptIn)
		{
			active = this;
			timer = SetTimer(nullptr, 0, 10, OnTimer);
		}
		~WorkflowRenameDriver()
		{
			if (timer) KillTimer(nullptr, timer);
			active = nullptr;
		}
		static void CALLBACK OnTimer(HWND, UINT, UINT_PTR, DWORD)
		{
			if (active && !active->visited)
				EnumThreadWindows(GetCurrentThreadId(), Visit, reinterpret_cast<LPARAM>(active));
		}
		static BOOL CALLBACK Visit(HWND window, LPARAM context)
		{
			auto &driver = *reinterpret_cast<WorkflowRenameDriver *>(context);
			if (!GetDlgItem(window, IDC_RENAME_LIST) || !GetDlgItem(window, IDC_TEMPLATE))
				return TRUE;
			driver.visited = true;
			SetDlgItemText(window, IDC_TEMPLATE, _T("Renamed ##"));
			SetDlgItemInt(window, IDC_START_AT, 7, FALSE);
			driver.ready = IsWindowEnabled(GetDlgItem(window, IDOK)) != FALSE;
			SendMessage(window, WM_COMMAND, MAKEWPARAM(driver.accept && driver.ready ? IDOK : IDCANCEL, BN_CLICKED), 0);
			return FALSE;
		}
	};

	struct WorkflowRenameSettings
	{
		CString pattern = App.Settings.Rename.Template;
		int position = App.Settings.Rename.Position;
		~WorkflowRenameSettings()
		{
			App.Settings.Rename.Template = pattern;
			App.Settings.Rename.Position = position;
		}
	};

	struct WorkflowPdfPrint : CPrintFolder
	{
		std::vector<UINT> pages;
		bool began = false;
		bool ended = false;
		bool cancelled = false;
		CPrintJob *activeJob = nullptr;
		DWORD *ownedJobId = nullptr;
		explicit WorkflowPdfPrint(State &state) : CPrintFolder(state) {}
		void BeginPrintJob(HDC) override
		{
			began = true;
			// WTL clears m_nJobID after EndDoc, before the spooler necessarily
			// finishes. Capture StartDoc's positive ID while it is still available.
			if (activeJob && ownedJobId && activeJob->m_nJobID > 0)
				*ownedJobId = static_cast<DWORD>(activeJob->m_nJobID);
		}
		void EndPrintJob(HDC, bool cancelledIn) override { ended = true; cancelled = cancelledIn; }
		bool PrintPage(UINT page, HDC dc) override
		{
			pages.push_back(page);
			return CPrintFolder::PrintPage(page, dc);
		}
	};

	struct WorkflowPdfJobOwner
	{
		enum class JobState { Gone, Active, Complete, Unknown };
		HANDLE printer;
		WorkflowTestFiles &fixture;
		CString output;
		CString title;
		DWORD jobId = 0;
		bool accepted = false;

		WorkflowPdfJobOwner(HANDLE printerIn, WorkflowTestFiles &fixtureIn, const CString &outputIn) :
			printer(printerIn), fixture(fixtureIn), output(outputIn),
			title(_T("ImageWalker PDF regression ") + fixtureIn.folder)
		{
		}

		static bool Missing(DWORD error)
		{
			return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
		}

		JobState Query() const
		{
			if (!jobId) return accepted ? JobState::Unknown : JobState::Gone;
			DWORD size = 0;
			::GetJob(printer, jobId, 1, nullptr, 0, &size);
			DWORD error = GetLastError();
			if (error == ERROR_INVALID_PARAMETER || Missing(error)) return JobState::Gone;
			if (error != ERROR_INSUFFICIENT_BUFFER || size == 0 || size > 1024 * 1024)
				return JobState::Unknown;
			std::vector<BYTE> storage(size);
			if (!::GetJob(printer, jobId, 1, storage.data(), size, &size))
			{
				error = GetLastError();
				return error == ERROR_INVALID_PARAMETER || Missing(error) ? JobState::Gone : JobState::Unknown;
			}
			const auto &info = *reinterpret_cast<const JOB_INFO_1 *>(storage.data());
			if (!info.pDocument) return JobState::Unknown;
			// A recycled ID must never cause cancellation of someone else's job.
			if (_tcscmp(info.pDocument, title) != 0) return JobState::Gone;
			const DWORD pending = JOB_STATUS_SPOOLING | JOB_STATUS_PRINTING | JOB_STATUS_DELETING |
				JOB_STATUS_ERROR | JOB_STATUS_RESTART;
			return !(info.Status & pending) && (info.Status & (JOB_STATUS_PRINTED | JOB_STATUS_COMPLETE)) ?
				JobState::Complete : JobState::Active;
		}

		static bool CompleteReadablePdf(const CString &path)
		{
			// Exclusive access rejects a writer that is still filling the file.
			const HANDLE file = CreateFile(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			LARGE_INTEGER size{};
			char header[5]{}, tail[64]{};
			DWORD read = 0;
			bool complete = GetFileSizeEx(file, &size) && size.QuadPart >= 15 &&
				ReadFile(file, header, sizeof(header), &read, nullptr) && read == sizeof(header) &&
				memcmp(header, "%PDF-", sizeof(header)) == 0;
			if (complete)
			{
				const DWORD length = size.QuadPart < static_cast<LONGLONG>(sizeof(tail)) ?
					static_cast<DWORD>(size.QuadPart) : static_cast<DWORD>(sizeof(tail));
				LARGE_INTEGER offset{};
				offset.QuadPart = -static_cast<LONGLONG>(length);
				complete = SetFilePointerEx(file, offset, nullptr, FILE_END) &&
					ReadFile(file, tail, length, &read, nullptr) && read == length;
				while (read > 0 && (tail[read - 1] == '\r' || tail[read - 1] == '\n' ||
					tail[read - 1] == ' ' || tail[read - 1] == '\t' || tail[read - 1] == '\f'))
					--read;
				complete = complete && read >= 5 && memcmp(tail + read - 5, "%%EOF", 5) == 0;
			}
			CloseHandle(file);
			return complete;
		}

		bool CancelAndDrain(DWORD timeout = 15000) const
		{
			const DWORD started = GetTickCount();
			bool requested = false;
			do
			{
				const JobState state = Query();
				if (state == JobState::Gone) return true;
				if (!requested && (state == JobState::Active || state == JobState::Complete))
					requested = ::SetJob(printer, jobId, 0, nullptr, JOB_CONTROL_CANCEL) != FALSE;
				Sleep(50);
			}
			while (GetTickCount() - started < timeout);
			return Query() == JobState::Gone;
		}

		bool WaitForComplete(DWORD timeout = 30000) const
		{
			const DWORD started = GetTickCount();
			do
			{
				const JobState state = Query();
				if (state == JobState::Gone && CompleteReadablePdf(output)) return true;
				// "Keep printed documents" retains a completed queue entry. Remove
				// only this finished job and still verify its writer has gone away.
				if (state == JobState::Complete && CompleteReadablePdf(output))
					return CancelAndDrain() && CompleteReadablePdf(output);
				Sleep(50);
			}
			while (GetTickCount() - started < timeout);
			return false;
		}

		~WorkflowPdfJobOwner()
		{
			bool drained = false, cleaned = false;
			try
			{
				drained = CancelAndDrain();
				if (drained)
				{
					const DWORD started = GetTickCount();
					do
					{
						if (DeleteFile(output) || Missing(GetLastError()))
						{
							cleaned = RemoveDirectory(fixture.folder) || Missing(GetLastError());
							if (cleaned) break;
						}
						Sleep(50);
					}
					while (GetTickCount() - started < 5000u);
				}
			}
			catch (const std::bad_alloc&)
			{
				// The fixture must not fall through to an unverified deletion.
			}
			IW_CHECK(drained);
			IW_CHECK(cleaned);
			if (!cleaned)
				IW::Test::Detail::Print("\r\n    ENVIRONMENT: PDF job %lu could not be drained/cleaned; retained %ls",
					jobId, static_cast<LPCTSTR>(fixture.folder));
			// This output was deliberately not registered with the fixture's
			// eager file deletion. On failure retain the artifact, visibly.
			fixture.folder.Empty();
		}
	};
}

IW_TEST(ModalRenameAcceptanceExecutesTheReviewedFilesAndCancelDoesNot)
{
	WorkflowRenameSettings restore;
	for (bool accept : {false, true})
	{
		WorkflowTestFiles fixture;
		IW_CHECK(!fixture.folder.IsEmpty());
		if (fixture.folder.IsEmpty()) return;
		const CString originals[] = {fixture.Path(_T("a.PNG")), fixture.Path(_T("b.PNG"))};
		const CString outputs[] = {fixture.Path(_T("Renamed 07.PNG")), fixture.Path(_T("Renamed 08.PNG"))};
		IW::FolderPtr folder = new IW::RefObj<IW::Folder>;
		for (int i = 0; i < 2; ++i)
		{
			IW_CHECK(WriteWorkflowPng(originals[i], i == 0 ? IW::RGBA(255, 0, 0) : IW::RGBA(0, 255, 0)));
			auto item = IW::FolderItem::CreateTestItem();
			IW_CHECK(item->Init(originals[i]));
			IW_CHECK(item->CanRename());
			item->ModifyFlags(THUMB_SELECTED, THUMB_SELECTED);
			folder->InsertThumb(item);
		}
		WorkflowRenameDriver driver(accept);
		IW_CHECK(driver.timer != 0);
		if (!driver.timer) return;
		{
			CRenameSelected rename;
			rename.Rename(folder);
		}
		IW_CHECK(driver.visited && driver.ready);
		State state(static_cast<Coupling *>(nullptr), false);
		CLoadAny loader(state.Loaders);
		for (int i = 0; i < 2; ++i)
		{
			IW_CHECK_EQ(IW::Path::FileExists(originals[i]), !accept);
			IW_CHECK_EQ(IW::Path::FileExists(outputs[i]), accept);
			IW::Image image;
			IW_CHECK(loader.LoadImage(accept ? outputs[i] : originals[i], image, IW::CNullStatus::Instance));
			if (image.IsEmpty()) continue;
			COLORREF pixel = 0;
			image.GetFirstPage().GetSurfaceLock()->GetLine(&pixel, 0, 0, 1);
			IW_CHECK_EQ(IW::GetR(pixel), i == 0 ? 255u : 0u);
			IW_CHECK_EQ(IW::GetG(pixel), i == 1 ? 255u : 0u);
		}
	}
}

IW_TEST(ToolbarConstructionPreservesCommandStyleBytesAndSeparators)
{
	IW_CHECK(AtlInitCommonControls(ICC_BAR_CLASSES));
	const HWND parent = CreateWindowEx(0, _T("STATIC"), _T("Toolbar test"), WS_POPUP,
		0, 0, 640, 480, nullptr, nullptr, _Module.GetModuleInstance(), nullptr);
	IW_CHECK(parent != nullptr);
	if (!parent) return;
	WorkflowDialogLifetime lifetime{parent};
	WorkflowCommandFrame frame;
	int commands[] = {ID_FILE_PRINT, 0, ID_VIEW_PRINT, -1};
	CToolBarCtrl toolbar(frame.CreateToolbar(parent, 1, commands));
	IW_CHECK(toolbar.IsWindow());
	if (!toolbar.IsWindow()) return;
	IW_CHECK_EQ(toolbar.GetButtonCount(), 3);
	for (int i = 0; i < 3; ++i)
	{
		TBBUTTON button{};
		IW_CHECK(toolbar.GetButton(i, &button));
		IW_CHECK_EQ(button.idCommand, commands[i]);
		if (commands[i] == 0)
			IW_CHECK_EQ(button.fsStyle, BTNS_SEP);
		else
		{
			for (const CommandInfo *command = s_commands; command->_id != -1; ++command)
				if (command->_id == commands[i])
					IW_CHECK_EQ(button.fsStyle, command->_nToolbarStyle | BTNS_AUTOSIZE);
		}
	}
}

IW_TEST(ContactSheetOptionsAndRunFlushTheFinalPartialPage)
{
	WorkflowTestFiles fixture;
	IW_CHECK(!fixture.folder.IsEmpty());
	if (fixture.folder.IsEmpty()) return;
	State state(static_cast<Coupling *>(nullptr), false);
	IW::FolderPtr folder = new IW::RefObj<IW::Folder>;
	state.Folder.SetFolder(folder);
	for (int i = 0; i < 5; ++i)
	{
		CString name;
		name.Format(_T("source%d.PNG"), i);
		const CString path = fixture.Path(name);
		IW_CHECK(WriteWorkflowPng(path, IW::RGBA(200, 20, 40)));
		auto item = IW::FolderItem::CreateTestItem();
		IW_CHECK(item->Init(path));
		folder->InsertThumb(item);
	}
	const CString first = fixture.Path(_T("Partial 1.PNG"));
	const CString last = fixture.Path(_T("Partial 2.PNG"));
	const CString extra = fixture.Path(_T("Partial 3.PNG"));
	CPrintFolder print(state);
	ConfigureWorkflowPrint(print);
	CToolContactSheet tool(print, state);
	tool._pathFolderOut = fixture.folder;
	tool._strOutputFile = _T("Partial");
	tool._sizeOutputImage = CSize(640, 480);
	const HWND window = tool.Create(nullptr);
	IW_CHECK(window != nullptr);
	if (!window) return;
	WorkflowDialogLifetime lifetime{window};
	tool.ShowWindow(SW_HIDE);
	IW_CHECK((GetWindowLongPtr(tool.GetDlgItem(IDC_FILES_ALL), GWL_STYLE) & WS_VISIBLE) == 0);
	IW_CHECK((GetWindowLongPtr(tool.GetDlgItem(IDC_OVERWRITE), GWL_STYLE) & WS_VISIBLE) == 0);
	tool.m_pLoaderFactory = state.Loaders.Find(CLoadPng::_GetKey());
	tool.m_pLoader = new IW::RefObj<CLoadPng>;
	IW_CHECK(tool.m_pLoaderFactory != nullptr);
	if (!tool.m_pLoaderFactory) return;
	tool.OnProcess(tool.GetStatus());
	IW_CHECK(IW::Path::FileExists(first));
	IW_CHECK(IW::Path::FileExists(last));
	IW_CHECK(!IW::Path::FileExists(extra));
	CLoadAny loader(state.Loaders);
	IW::Image image;
	IW_CHECK(loader.LoadImage(last, image, IW::CNullStatus::Instance));
	if (image.IsEmpty()) return;
	auto lock = image.GetFirstPage().GetSurfaceLock();
	COLORREF pixel = 0;
	lock->GetLine(&pixel, 120, 160, 1);
	IW_CHECK_EQ(IW::GetR(pixel), 200u);
	IW_CHECK_EQ(IW::GetG(pixel), 20u);
	IW_CHECK_EQ(IW::GetB(pixel), 40u);
	lock->GetLine(&pixel, 120, 480, 1);
	IW_CHECK_EQ(pixel & 0xffffffu, 0xffffffu);
	lock->GetLine(&pixel, 360, 160, 1);
	IW_CHECK_EQ(pixel & 0xffffffu, 0xffffffu);
}

IW_TEST(PdfCompletionRejectsPartialOutputAndAnActiveWriter)
{
	WorkflowTestFiles fixture;
	IW_CHECK(!fixture.folder.IsEmpty());
	if (fixture.folder.IsEmpty()) return;
	const CString path = fixture.Path(_T("completion.pdf"));
	HANDLE file = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	IW_CHECK(file != INVALID_HANDLE_VALUE);
	if (file == INVALID_HANDLE_VALUE) return;
	const char header[] = "%PDF-1.7\ntrailer\n";
	DWORD written = 0;
	IW_CHECK(WriteFile(file, header, sizeof(header) - 1, &written, nullptr));
	IW_CHECK_EQ(written, sizeof(header) - 1);
	CloseHandle(file);
	IW_CHECK(!WorkflowPdfJobOwner::CompleteReadablePdf(path));

	file = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	IW_CHECK(file != INVALID_HANDLE_VALUE);
	if (file == INVALID_HANDLE_VALUE) return;
	const char trailer[] = "%%EOF\r\n";
	IW_CHECK(WriteFile(file, trailer, sizeof(trailer) - 1, &written, nullptr));
	IW_CHECK_EQ(written, sizeof(trailer) - 1);
	IW_CHECK(FlushFileBuffers(file));
	IW_CHECK(!WorkflowPdfJobOwner::CompleteReadablePdf(path));
	CloseHandle(file);
	IW_CHECK(WorkflowPdfJobOwner::CompleteReadablePdf(path));
}

IW_TEST(NativeSpoolerRendersOnlyToAnExplicitMicrosoftPdfFile)
{
	// Missing virtual-printer support is a reported failure, never a successful
	// skip. Never fall back to the default printer, OneNote, or a physical port.
	CPrinter printer;
	const bool opened = printer.OpenPrinter(_T("Microsoft Print to PDF"));
	if (!opened)
		IW::Test::Detail::Print("\r\n    ENVIRONMENT: Microsoft Print to PDF is required for the spooler test.");
	IW_CHECK(opened);
	if (!opened) return;
	CPrinterInfo<2> info;
	const bool queried = info.GetPrinterInfo(printer);
	IW_CHECK(queried);
	if (!queried || !info.m_pi) return;
	const auto &pi = *info.m_pi;
	const bool safe = pi.pPrinterName && pi.pDriverName && pi.pPortName &&
		_tcsicmp(pi.pPrinterName, _T("Microsoft Print to PDF")) == 0 &&
		_tcsicmp(pi.pDriverName, _T("Microsoft Print To PDF")) == 0 &&
		_tcsicmp(pi.pPortName, _T("PORTPROMPT:")) == 0 &&
		(!pi.pServerName || !*pi.pServerName) && !(pi.Attributes & PRINTER_ATTRIBUTE_NETWORK);
	IW_CHECK(safe);
	if (!safe) return;

	WorkflowTestFiles fixture;
	IW_CHECK(!fixture.folder.IsEmpty());
	if (fixture.folder.IsEmpty()) return;
	const CString output = IW::Path::Combine(fixture.folder, _T("print.pdf"));
	IW_CHECK(!PathIsRelative(output) && !IW::Path::FileExists(output));
	if (PathIsRelative(output) || IW::Path::FileExists(output)) return;
	State state(static_cast<Coupling *>(nullptr), false);
	IW::FolderPtr folder = new IW::RefObj<IW::Folder>;
	state.Folder.SetFolder(folder);
	for (int i = 0; i < 5; ++i)
	{
		auto item = IW::FolderItem::CreateTestItem();
		item->SetImage(WorkflowSolidImage(IW::RGBA(200, 20, 40)));
		folder->InsertThumb(item);
	}
	WorkflowPdfPrint print(state);
	ConfigureWorkflowPrint(print);
	CDevMode mode;
	const bool copiedMode = mode.CopyFromPrinter(printer);
	IW_CHECK(copiedMode);
	if (!copiedMode) return;
	CDC dc;
	dc.Attach(printer.CreatePrinterDC(mode));
	IW_CHECK(!dc.IsNull());
	if (dc.IsNull()) return;
	const bool laidOut = print.CalcLayout(dc.m_hDC);
	IW_CHECK(laidOut);
	if (!laidOut) return;
	PRINTDLG dialog{};
	IW::SetPrintDialogPageRange(dialog, print.m_nMaxPage);
	unsigned long first = 0, last = 0;
	const bool validRange = IW::GetPrintJobPageRange(dialog, print.m_nMaxPage, first, last);
	IW_CHECK(validRange);
	if (!validRange) return;
	CPrintJob job;
	WorkflowPdfJobOwner owner(printer, fixture, output);
	print.activeJob = &job;
	print.ownedJobId = &owner.jobId;
	const bool printed = job.StartPrintJob(false, printer, mode, &print, owner.title,
		first, last, true, output);
	owner.accepted = printed;
	IW_CHECK(printed && job.IsJobComplete());
	IW_CHECK(owner.jobId != 0);
	IW_CHECK(print.began && print.ended && !print.cancelled);
	IW_CHECK((print.pages == std::vector<UINT>{1, 2}));
	const bool complete = printed && owner.jobId != 0 && owner.WaitForComplete();
	IW_CHECK(complete);
	// The owner cancels/drains a timeout (and any retained completed job)
	// before deleting the output and verifying removal of the fixture folder.
}
