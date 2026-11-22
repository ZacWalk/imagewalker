#pragma once

#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

class CText
{
public:
	CText() = default;
	CText(const char* text) : bytes_(text ? text : "") {}
	explicit CText(std::string text) : bytes_(std::move(text)) {}
	operator const char*() const { return bytes_.c_str(); }
	bool IsEmpty() const { return bytes_.empty(); }
	int GetLength() const { return static_cast<int>(bytes_.size()); }
	void Empty() { bytes_.clear(); }
	void Remove(char ch) { std::erase(bytes_, ch); }
	void Trim()
	{
		const auto start = bytes_.find_first_not_of(" \t\r\n");
		if (start == std::string::npos) bytes_.clear();
		else bytes_ = bytes_.substr(start, bytes_.find_last_not_of(" \t\r\n") - start + 1);
	}
	int Find(char ch) const
	{
		const auto pos = bytes_.find(ch);
		return pos == std::string::npos ? -1 : static_cast<int>(pos);
	}
	int CompareNoCase(const char* text) const { return _stricmp(bytes_.c_str(), text); }
	CText Left(int n) const { return CText(bytes_.substr(0, n)); }
	CText Mid(int n) const { return CText(bytes_.substr(n)); }
	CText Right(int n) const { return CText(bytes_.substr(bytes_.size() - (std::min)(bytes_.size(), size_t(n)))); }
	char* GetBufferSetLength(int n) { bytes_.resize(n); return bytes_.data(); }
	void ReleaseBuffer() { bytes_.resize(strlen(bytes_.c_str())); }
	char operator[](int i) const { return bytes_[i]; }
	CText Mid(int start, int count) const { return CText(bytes_.substr(start, count)); }
	bool LoadString(UINT id)
	{
		const wchar_t* text{};
		const int count = ::LoadStringW(::GetModuleHandleW(nullptr), id, reinterpret_cast<LPWSTR>(&text), 0);
		if (!count) { Empty(); return false; }
		const int size = ::WideCharToMultiByte(CP_ACP, 0, text, count, nullptr, 0, nullptr, nullptr);
		bytes_.resize(size);
		return size && ::WideCharToMultiByte(CP_ACP, 0, text, count, bytes_.data(), size, nullptr, nullptr) == size;
	}
	void Format(const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		bytes_ = formatted(format, args);
		va_end(args);
	}
	void AppendFormat(const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		bytes_ += formatted(format, args);
		va_end(args);
	}
	bool operator==(const CText&) const = default;
	bool operator==(const char* text) const { return bytes_ == text; }
	CText& operator+=(const CText& rhs) { bytes_ += rhs.bytes_; return *this; }
	friend CText operator+(CText lhs, const CText& rhs) { return lhs += rhs; }

private:
	static std::string formatted(const char* format, va_list args)
	{
		va_list measure;
		va_copy(measure, args);
		const int size = std::vsnprintf(nullptr, 0, format, measure);
		va_end(measure);
		if (size <= 0) return {};
		std::string text(size, '\0');
		std::vsnprintf(text.data(), text.size() + 1, format, args);
		return text;
	}
	std::string bytes_;
};

struct CSize : SIZE
{
	CSize(LONG w = 0, LONG h = 0) : SIZE{w, h} {}
	CSize(SIZE size) : SIZE(size) {}
	bool operator==(CSize b) const { return cx == b.cx && cy == b.cy; }
};

struct CPoint : POINT
{
	CPoint(LONG x = 0, LONG y = 0) : POINT{x, y} {}
	CPoint(POINT point) : POINT(point) {}
	CPoint operator+(CSize size) const { return {x + size.cx, y + size.cy}; }
	CPoint operator+(CPoint b) const { return {x + b.x, y + b.y}; }
	CSize operator-(CPoint b) const { return {x - b.x, y - b.y}; }
	CPoint operator-() const { return {-x, -y}; }
	CPoint& operator+=(CPoint other) { x += other.x; y += other.y; return *this; }
	bool operator==(CPoint b) const { return x == b.x && y == b.y; }
};

inline CPoint MessagePoint(LPARAM value)
{
	return {static_cast<short>(LOWORD(value)), static_cast<short>(HIWORD(value))};
}

struct CRect : RECT
{
	CRect(LONG l = 0, LONG t = 0, LONG r = 0, LONG b = 0) : RECT{l, t, r, b} {}
	CRect(RECT rect) : RECT(rect) {}
	CRect(CPoint a, CSize size) : CRect(a.x, a.y, a.x + size.cx, a.y + size.cy) {}
	CRect(CPoint a, CPoint b) : CRect(a.x, a.y, b.x, b.y) {}
	int Width() const { return right - left; }
	int Height() const { return bottom - top; }
	CSize Size() const { return {Width(), Height()}; }
	CPoint TopLeft() const { return {left, top}; }
	CPoint CenterPoint() const { return {left + Width() / 2, top + Height() / 2}; }
	BOOL IsRectEmpty() const { return ::IsRectEmpty(this); }
	bool IsRectNull() const { return *this == CRect{}; }
	BOOL PtInRect(CPoint point) const { return ::PtInRect(this, point); }
	void SetRect(int l, int t, int r, int b) { ::SetRect(this, l, t, r, b); }
	void SetRectEmpty() { ::SetRectEmpty(this); }
	void InflateRect(int x, int y) { ::InflateRect(this, x, y); }
	void DeflateRect(int x, int y) { ::InflateRect(this, -x, -y); }
	void OffsetRect(CPoint p) { ::OffsetRect(this, p.x, p.y); }
	void OffsetRect(CSize s) { ::OffsetRect(this, s.cx, s.cy); }
	BOOL IntersectRect(const RECT& a, const RECT& b) { return ::IntersectRect(this, &a, &b); }
	CRect operator&(const RECT& b) const { CRect r; r.IntersectRect(*this, b); return r; }
	CRect& operator+=(CPoint p) { OffsetRect(p); return *this; }
	CRect& operator-=(CPoint p) { OffsetRect(-p); return *this; }
	CRect operator+(CPoint p) const { CRect r(*this); return r += p; }
	bool operator==(const CRect& b) const { return ::EqualRect(this, &b) != FALSE; }
};

inline CText WindowText(HWND window)
{
	CText text;
	const int length = ::GetWindowTextLengthA(window);
	::GetWindowTextA(window, text.GetBufferSetLength(length + 1), length + 1);
	text.ReleaseBuffer();
	return text;
}

class CWindow
{
public:
	CWindow(HWND window = nullptr) : m_hWnd(window) {}
	operator HWND() const { return m_hWnd; }
	BOOL IsWindow() const { return ::IsWindow(m_hWnd); }
	BOOL IsWindowVisible() const { return ::IsWindowVisible(m_hWnd); }
	BOOL IsWindowEnabled() const { return ::IsWindowEnabled(m_hWnd); }
	BOOL IsIconic() const { return ::IsIconic(m_hWnd); }
	BOOL EnableWindow(BOOL enabled) const { return ::EnableWindow(m_hWnd, enabled); }
	BOOL ShowWindow(int command) const { return ::ShowWindow(m_hWnd, command); }
	BOOL DestroyWindow() const { return ::DestroyWindow(m_hWnd); }
	BOOL UpdateWindow() const { return ::UpdateWindow(m_hWnd); }
	HWND SetFocus() const { return ::SetFocus(m_hWnd); }
	HWND SetCapture() const { return ::SetCapture(m_hWnd); }
	HWND GetParent() const { return ::GetParent(m_hWnd); }
	HWND GetTopLevelParent() const { return ::GetAncestor(m_hWnd, GA_ROOT); }
	HWND GetDlgItem(int id) const { return ::GetDlgItem(m_hWnd, id); }
	DWORD GetStyle() const { return static_cast<DWORD>(::GetWindowLongPtrA(m_hWnd, GWL_STYLE)); }
	BOOL GetClientRect(RECT* rect) const { return ::GetClientRect(m_hWnd, rect); }
	BOOL GetWindowRect(RECT* rect) const { return ::GetWindowRect(m_hWnd, rect); }
	BOOL ScreenToClient(POINT* point) const { return ::ScreenToClient(m_hWnd, point); }
	void ScreenToClient(RECT* rect) const { ::MapWindowPoints(nullptr, m_hWnd, reinterpret_cast<POINT*>(rect), 2); }
	void ClientToScreen(RECT* rect) const { ::MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<POINT*>(rect), 2); }
	BOOL Invalidate(BOOL erase = TRUE) const { return ::InvalidateRect(m_hWnd, nullptr, erase); }
	BOOL InvalidateRect(const RECT& rect, BOOL erase) const { return ::InvalidateRect(m_hWnd, &rect, erase); }
	BOOL SetWindowPos(HWND after, int x, int y, int width, int height, UINT flags) const
	{
		return ::SetWindowPos(m_hWnd, after, x, y, width, height, flags);
	}
	int ScrollWindowEx(int x, int y, const RECT* area, const RECT* clip, HRGN update, RECT* bounds, UINT flags) const
	{
		return ::ScrollWindowEx(m_hWnd, x, y, area, clip, update, bounds, flags);
	}
	LRESULT SendMessage(UINT msg, WPARAM w = 0, LPARAM l = 0) const { return ::SendMessageA(m_hWnd, msg, w, l); }
	BOOL PostMessage(UINT msg, WPARAM w = 0, LPARAM l = 0) const { return ::PostMessageA(m_hWnd, msg, w, l); }
	void SetWindowText(const char* text) const { ::SetWindowTextA(m_hWnd, text); }
	void GetWindowText(CText& text) const { text = WindowText(m_hWnd); }
	UINT_PTR SetTimer(UINT_PTR id, UINT delay) const { return ::SetTimer(m_hWnd, id, delay, nullptr); }
	void KillTimer(UINT_PTR id) const { ::KillTimer(m_hWnd, id); }

	HWND m_hWnd{};

protected:
	HWND CreateControl(const char* klass, HWND parent, const RECT& r, const char* title,
	                   DWORD style, DWORD exStyle, int id)
	{
		m_hWnd = ::CreateWindowExA(exStyle, klass, title, style, r.left, r.top, r.right - r.left,
		                          r.bottom - r.top, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
		                          ::GetModuleHandleA(nullptr), nullptr);
		return m_hWnd;
	}
};

class SdkWindow : public CWindow
{
protected:
	HWND CreateNative(const char* name, HWND parent, RECT rect, const char* title, DWORD style,
	                  DWORD exStyle = 0, UINT classStyle = 0, HICON icon = nullptr, HMENU menu = nullptr);
	virtual LRESULT WindowMessage(UINT message, WPARAM wParam, LPARAM lParam)
	{
		return ::DefWindowProcA(m_hWnd, message, wParam, lParam);
	}

private:
	static LRESULT CALLBACK Dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
};

class GdiDraw
{
public:
	explicit GdiDraw(HDC dc = nullptr) : m_hDC(dc) {}
	operator HDC() const { return m_hDC; }
	int GetClipBox(RECT* r) const { return ::GetClipBox(m_hDC, r); }
	BOOL GetTextMetrics(TEXTMETRICA* metrics) const { return ::GetTextMetricsA(m_hDC, metrics); }
	HFONT SelectFont(HFONT font) const { return reinterpret_cast<HFONT>(::SelectObject(m_hDC, font)); }
	int FillRect(const RECT* r, HBRUSH brush) const { return ::FillRect(m_hDC, r, brush); }
	int FrameRect(const RECT& r, HBRUSH brush) const { return ::FrameRect(m_hDC, &r, brush); }
	BOOL DrawFocusRect(const RECT* r) const { return ::DrawFocusRect(m_hDC, r); }
	COLORREF SetTextColor(COLORREF colour) const { return ::SetTextColor(m_hDC, colour); }
	void SetBkMode(int mode) const { ::SetBkMode(m_hDC, mode); }
	void SetViewportOrg(int x, int y) const { ::SetViewportOrgEx(m_hDC, x, y, nullptr); }
	int DrawText(const char* text, int length, RECT& r, UINT flags) const
	{
		return ::DrawTextA(m_hDC, text, length, &r, flags);
	}
	HDC m_hDC{};
};

class CClientDC : public GdiDraw
{
public:
	explicit CClientDC(HWND window) : GdiDraw(::GetDC(window)), window_(window) {}
	~CClientDC() { ::ReleaseDC(window_, m_hDC); }
private:
	HWND window_;
};

class CPaintDC : public GdiDraw
{
public:
	explicit CPaintDC(HWND window) : window_(window) { m_hDC = ::BeginPaint(window, &paint_); }
	~CPaintDC() { ::EndPaint(window_, &paint_); }
private:
	HWND window_;
	PAINTSTRUCT paint_{};
};

class CWaitCursor
{
	HCURSOR previous_ = ::SetCursor(::LoadCursorA(nullptr, IDC_WAIT));
public:
	~CWaitCursor() { ::SetCursor(previous_); }
};

inline HFONT DefaultGuiFont() { return static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)); }

inline CText NarrowText(const wchar_t* text)
{
	CText result;
	const int length = ::WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (length > 1)
	{
		::WideCharToMultiByte(CP_ACP, 0, text, -1, result.GetBufferSetLength(length), length, nullptr, nullptr);
		result.ReleaseBuffer();
	}
	return result;
}

inline std::wstring WideText(const char* text)
{
	const int length = ::MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
	if (length <= 1) return {};
	std::wstring result(length, L'\0');
	if (!::MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text, -1, result.data(), length))
		return {};
	result.pop_back();
	return result;
}

inline void TrackResourceMenu(int resource, int index, UINT flags, CPoint position, HWND owner)
{
	const HMENU menu = ::LoadMenuA(::GetModuleHandleA(nullptr), MAKEINTRESOURCEA(resource));
	if (menu)
	{
		if (const HMENU popup = ::GetSubMenu(menu, index))
			::TrackPopupMenu(popup, flags, position.x, position.y, 0, owner, nullptr);
		::DestroyMenu(menu);
	}
}

struct CScrollBar : CWindow
{
	using CWindow::CWindow;
	HWND Create(HWND parent, RECT r, const char* title, DWORD style, DWORD ex, int id)
	{ return CreateControl("SCROLLBAR", parent, r, title, style, ex, id); }
	void SetScrollPos(int position, BOOL redraw) const { ::SetScrollPos(m_hWnd, SB_CTL, position, redraw); }
	void SetScrollInfo(SCROLLINFO* info, BOOL redraw) const { ::SetScrollInfo(m_hWnd, SB_CTL, info, redraw); }
};

struct CTrackBarCtrl : CWindow
{
	HWND Create(HWND parent, RECT r, const char* title, DWORD style, DWORD ex, int id)
	{ return CreateControl(TRACKBAR_CLASSA, parent, r, title, style, ex, id); }
	void SetRange(int low, int high, BOOL redraw) const { SendMessage(TBM_SETRANGE, redraw, MAKELPARAM(low, high)); }
	void SetLineSize(int size) const { SendMessage(TBM_SETLINESIZE, 0, size); }
	void SetPageSize(int size) const { SendMessage(TBM_SETPAGESIZE, 0, size); }
	void SetPos(int position) const { SendMessage(TBM_SETPOS, TRUE, position); }
	int GetPos() const { return static_cast<int>(SendMessage(TBM_GETPOS)); }
};

struct CStatusBarCtrl : CWindow
{
	HWND Create(HWND parent, RECT r, const char* title, DWORD style, DWORD ex, int id)
	{ return CreateControl(STATUSCLASSNAMEA, parent, r, title, style, ex, id); }
	void SetParts(int count, const int* parts) const { SendMessage(SB_SETPARTS, count, reinterpret_cast<LPARAM>(parts)); }
	void SetText(int pane, const char* text, UINT flags) const
	{ SendMessage(SB_SETTEXTA, pane | flags, reinterpret_cast<LPARAM>(text)); }
};

struct CEdit : CWindow
{
	HWND Create(HWND parent, RECT r, const char* title, DWORD style, DWORD ex, int id)
	{ return CreateControl("EDIT", parent, r, title, style, ex, id); }
	void SetFont(HFONT font) const { SendMessage(WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); }
	void LimitText(int limit) const { SendMessage(EM_LIMITTEXT, limit); }
};

struct CToolBarCtrl : CWindow
{
	~CToolBarCtrl()
	{
		if (images_) ::ImageList_Destroy(images_);
		if (hot_) ::ImageList_Destroy(hot_);
	}
	HWND Create(HWND parent, RECT r, const char* title, DWORD style, DWORD ex, int id)
	{ return CreateControl(TOOLBARCLASSNAMEA, parent, r, title, style, ex, id); }
	void SetButtonStructSize(size_t size) const { SendMessage(TB_BUTTONSTRUCTSIZE, size); }
	void SetButtonSize(CSize s) const { SendMessage(TB_SETBUTTONSIZE, 0, MAKELPARAM(s.cx, s.cy)); }
	void SetBitmapSize(CSize s) const { SendMessage(TB_SETBITMAPSIZE, 0, MAKELPARAM(s.cx, s.cy)); }
	void AddButtons(int n, const TBBUTTON* buttons) const
	{ SendMessage(TB_ADDBUTTONSA, n, reinterpret_cast<LPARAM>(buttons)); }
	void SetImageList(HIMAGELIST images) { images_ = images; SendMessage(TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images)); }
	void SetHotImageList(HIMAGELIST images) { hot_ = images; SendMessage(TB_SETHOTIMAGELIST, 0, reinterpret_cast<LPARAM>(images)); }
	int GetButtonCount() const { return static_cast<int>(SendMessage(TB_BUTTONCOUNT)); }
	void GetItemRect(int index, RECT* r) const { SendMessage(TB_GETITEMRECT, index, reinterpret_cast<LPARAM>(r)); }
	void GetRect(int id, RECT* r) const { SendMessage(TB_GETRECT, id, reinterpret_cast<LPARAM>(r)); }
	int GetState(int id) const { return static_cast<int>(SendMessage(TB_GETSTATE, id)); }
	void CheckButton(int id, BOOL value) const { SendMessage(TB_CHECKBUTTON, id, MAKELPARAM(value, 0)); }
	void EnableButton(int id, BOOL value) const { SendMessage(TB_ENABLEBUTTON, id, MAKELPARAM(value, 0)); }
	int HitTest(POINT* p) const { return static_cast<int>(SendMessage(TB_HITTEST, 0, reinterpret_cast<LPARAM>(p))); }
	int CommandToIndex(int id) const { return static_cast<int>(SendMessage(TB_COMMANDTOINDEX, id)); }
private:
	HIMAGELIST images_{}, hot_{};
};

struct CToolTipCtrl : CWindow
{
	HWND Create(HWND parent, DWORD style)
	{ return CreateControl(TOOLTIPS_CLASSA, parent, {}, nullptr, WS_POPUP | style, 0, 0); }
	BOOL AddTool(HWND tool) const
	{
		TOOLINFOA info{sizeof(info)};
		info.hwnd = tool;
		info.uId = reinterpret_cast<UINT_PTR>(tool);
		info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		info.lpszText = LPSTR_TEXTCALLBACKA;
		return static_cast<BOOL>(SendMessage(TTM_ADDTOOLA, 0, reinterpret_cast<LPARAM>(&info)));
	}
	void Activate(BOOL value) const { SendMessage(TTM_ACTIVATE, value); }
	void SetDelayTime(DWORD type, int delay) const { SendMessage(TTM_SETDELAYTIME, type, delay); }
	void SetMaxTipWidth(int width) const { SendMessage(TTM_SETMAXTIPWIDTH, 0, width); }
};

inline HIMAGELIST LoadToolbarImages(int resource, int width)
{
	return ::ImageList_LoadImageA(::GetModuleHandleA(nullptr), MAKEINTRESOURCEA(resource), width, 1,
	                              RGB(255, 0, 255), IMAGE_BITMAP, LR_CREATEDIBSECTION);
}
