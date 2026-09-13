#pragma once

// Tests for the shared portable-app plumbing. Every app's test.cpp includes
// this so a regression in appinfo/inifile shows up in all five, whichever one
// you happen to run.

#include "iw/appinfo.h"
#include "iw/cursorautohide.h"
#include "iw/inifile.h"
#include "iw/testrunner.h"
#include "iw/workqueue.h"

IW_TEST(CursorAutoHideStaysInsideItsOwnWindow)
{
	// Never a real window, so nothing is sent and nothing outside this test can
	// be affected - which is the whole point of the WM_SETCURSOR approach.
	HWND hwnd = reinterpret_cast<HWND>(0x1234);
	const WPARAM self = reinterpret_cast<WPARAM>(hwnd);
	const LPARAM client = MAKELPARAM(HTCLIENT, WM_MOUSEMOVE);
	const LPARAM caption = MAKELPARAM(HTCAPTION, WM_MOUSEMOVE);

	// An idle period no session can reach, so "not idle enough yet" is testable.
	const DWORD never = 0xFFFFFFFF;

	IW::CCursorAutoHide hide;

	IW_CHECK(!hide.IsCursorHidden());
	IW_CHECK(!hide.OnSetCursor(hwnd, self, client));

	IW_CHECK(!hide.Update(hwnd, false, 0));
	IW_CHECK(!hide.IsCursorHidden());
	IW_CHECK(!hide.Update(hwnd, true, never));
	IW_CHECK(!hide.IsCursorHidden());

	IW_CHECK(hide.Update(hwnd, true, 0));
	IW_CHECK(hide.IsCursorHidden());
	IW_CHECK(!hide.Update(hwnd, true, 0));

	// Hidden, but only ever over our own client area.
	IW_CHECK(hide.OnSetCursor(hwnd, self, client));
	IW_CHECK(!hide.OnSetCursor(hwnd, self, caption));
	IW_CHECK(!hide.OnSetCursor(hwnd, 0, client));

	IW_CHECK(hide.Show(hwnd));
	IW_CHECK(!hide.IsCursorHidden());
	IW_CHECK(!hide.OnSetCursor(hwnd, self, client));

	// Turning hiding off puts it back with nobody having to move the mouse.
	IW_CHECK(hide.Update(hwnd, true, 0));
	IW_CHECK(hide.Update(hwnd, false, 0));
	IW_CHECK(!hide.IsCursorHidden());
}

IW_TEST(AppPathsDeriveFromTheExecutable)
{
	const IW::Paths::String &stem = IW::Paths::ModuleStem();
	IW_CHECK(!stem.empty());
	IW_CHECK(stem.find(_T('.')) == IW::Paths::String::npos);
	IW_CHECK(stem.find(_T('\\')) == IW::Paths::String::npos);

	IW_CHECK(!IW::Paths::ModuleDir().empty());
	IW_CHECK(IW::Paths::ModuleDir()[IW::Paths::ModuleDir().size() - 1] == _T('\\'));

	IW_CHECK(IW::Paths::IniPath() == IW::Paths::ModuleDir() + stem + _T(".ini"));
	IW_CHECK(IW::Paths::LogPath() == IW::Paths::ModuleDir() + stem + _T(".log"));
}

IW_TEST(IniRoundTripsEveryValueType)
{
	LPCTSTR section = _T("TestScratch");

	IW_CHECK(IW::Ini::WriteString(section, _T("text"), _T("hello world")));
	IW_CHECK(IW::Ini::ReadString(section, _T("text"), _T("")) == IW::Paths::String(_T("hello world")));

	IW_CHECK(IW::Ini::WriteDword(section, _T("dword"), 0xDEADBEEF));
	DWORD dword = 0;
	IW_CHECK(IW::Ini::ReadDword(section, _T("dword"), dword));
	IW_CHECK_EQ(dword, 0xDEADBEEF);

	IW_CHECK(IW::Ini::WriteInt(section, _T("negative"), -12345));
	int number = 0;
	IW_CHECK(IW::Ini::ReadInt(section, _T("negative"), number));
	IW_CHECK_EQ(number, -12345);

	IW_CHECK(IW::Ini::WriteBool(section, _T("flag"), true));
	bool flag = false;
	IW_CHECK(IW::Ini::ReadBool(section, _T("flag"), flag));
	IW_CHECK_EQ(flag, true);

	const BYTE blob[] = { 0x00, 0x01, 0x7F, 0x80, 0xFF };
	IW_CHECK(IW::Ini::WriteBinary(section, _T("blob"), blob, sizeof(blob)));

	std::vector<BYTE> readback;
	IW_CHECK(IW::Ini::ReadBinary(section, _T("blob"), readback));
	IW_CHECK_EQ(readback.size(), sizeof(blob));

	if (readback.size() == sizeof(blob))
		IW_CHECK(memcmp(&readback[0], blob, sizeof(blob)) == 0);

	IW::Ini::DeleteSection(section);
	IW_CHECK(!IW::Ini::ReadDword(section, _T("dword"), dword));
}

IW_TEST(IniDistinguishesMissingFromEmpty)
{
	LPCTSTR section = _T("TestScratch");

	IW::Ini::DeleteSection(section);

	IW::Paths::String value(_T("untouched"));
	IW_CHECK(!IW::Ini::ReadString(section, _T("absent"), value));

	IW_CHECK(IW::Ini::WriteString(section, _T("present"), _T("")));
	IW_CHECK(IW::Ini::ReadString(section, _T("present"), value));
	IW_CHECK(value.empty());

	IW::Ini::DeleteSection(section);
}

IW_TEST(IniSectionPathNests)
{
	IW_CHECK(IW::Ini::SectionPath(_T(""), _T("Toolbar")) == IW::Paths::String(_T("Toolbar")));
	IW_CHECK(IW::Ini::SectionPath(_T("Settings"), _T("")) == IW::Paths::String(_T("Settings")));
	IW_CHECK(IW::Ini::SectionPath(_T("Settings"), _T("Toolbar")) ==
	         IW::Paths::String(_T("Settings\\Toolbar")));
}

namespace
{
	struct CountingItem : IW::CWorkItem
	{
		int worked = 0;
		int completed = 0;

		void Work() override { ++worked; }
		void Complete() override { ++completed; }
	};
}

IW_TEST(WorkQueueSignalsWhileItHasItems)
{
	IW::CWorkQueue<int> queue;

	IW_CHECK_EQ(::WaitForSingleObject(queue.Handle(), 0), (DWORD)WAIT_TIMEOUT);
	IW_CHECK(!queue.Pop());

	queue.Push(std::make_shared<int>(1));
	queue.Push(std::make_shared<int>(2));
	IW_CHECK_EQ(queue.Size(), (size_t)2);
	IW_CHECK_EQ(::WaitForSingleObject(queue.Handle(), 0), (DWORD)WAIT_OBJECT_0);

	IW_CHECK_EQ(*queue.Pop(), 1);
	IW_CHECK_EQ(::WaitForSingleObject(queue.Handle(), 0), (DWORD)WAIT_OBJECT_0);
	IW_CHECK_EQ(*queue.Pop(), 2);
	IW_CHECK_EQ(::WaitForSingleObject(queue.Handle(), 0), (DWORD)WAIT_TIMEOUT);
}

IW_TEST(WorkQueueCloseReleasesWaitersAndDropsItems)
{
	IW::CWorkQueue<int> queue;

	queue.Push(std::make_shared<int>(1));
	queue.Close();

	IW_CHECK(queue.IsClosed());
	IW_CHECK_EQ(queue.Size(), (size_t)0);
	IW_CHECK(!queue.Push(std::make_shared<int>(2)));

	// Take must return rather than block, and the event must stay signalled so
	// a second waiter is released too. That is the whole stop protocol.
	IW_CHECK(!queue.Take());
	IW_CHECK_EQ(::WaitForSingleObject(queue.Handle(), 0), (DWORD)WAIT_OBJECT_0);
}

IW_TEST(WorkerThreadRunsWorkThenHandsBackForCompletion)
{
	// UiQueue is process-wide; a stale item would satisfy the wait below.
	IW::UiQueue().DrainAll();
	IW_CHECK_EQ(IW::UiQueue().Size(), (size_t)0);

	auto owner = std::make_shared<int>(0);
	auto item = std::make_shared<CountingItem>();
	item->SetOwner(owner);

	IW::CWorkerThread worker;
	IW_CHECK(worker.Start());
	IW_CHECK(worker.Post(item));

	IW_CHECK_EQ(::WaitForSingleObject(IW::UiQueue().Handle(), 5000), (DWORD)WAIT_OBJECT_0);
	IW::UiQueue().DrainAll();

	IW_CHECK_EQ(item->worked, 1);
	IW_CHECK_EQ(item->completed, 1);

	worker.Stop();
}

IW_TEST(DispatchQueueSkipsItemsWhoseOwnerHasGone)
{
	IW::UiQueue().DrainAll();
	IW_CHECK_EQ(IW::UiQueue().Size(), (size_t)0);

	auto owner = std::make_shared<int>(0);
	auto orphan = std::make_shared<CountingItem>();
	orphan->SetOwner(owner);
	owner.reset();

	auto ownerless = std::make_shared<CountingItem>();

	IW::UiQueue().Push(orphan);
	IW::UiQueue().Push(ownerless);
	IW::UiQueue().DrainAll();

	IW_CHECK_EQ(orphan->completed, 0);
	IW_CHECK_EQ(ownerless->completed, 1);
}
