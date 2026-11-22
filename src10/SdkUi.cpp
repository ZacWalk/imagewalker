#include "stdafx.h"
#include "iw/logfile.h"

HWND SdkWindow::CreateNative(const char* name, HWND parent, RECT r, const char* title, DWORD style,
                             DWORD exStyle, UINT classStyle, HICON icon, HMENU menu)
{
	const HINSTANCE instance = ::GetModuleHandleA(nullptr);
	WNDCLASSEXA klass{sizeof(klass)};
	klass.style = classStyle;
	klass.lpfnWndProc = Dispatch;
	klass.hInstance = instance;
	klass.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
	klass.hIcon = icon;
	klass.lpszClassName = name;
	if (!::RegisterClassExA(&klass) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return nullptr;
	const int width = r.right == CW_USEDEFAULT ? CW_USEDEFAULT : r.right - r.left;
	const int height = r.bottom == CW_USEDEFAULT ? CW_USEDEFAULT : r.bottom - r.top;
	return ::CreateWindowExA(exStyle, name, title, style, r.left, r.top, width, height,
	                        parent, menu, instance, this);
}

LRESULT CALLBACK SdkWindow::Dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto* self = reinterpret_cast<SdkWindow*>(::GetWindowLongPtrA(window, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		const auto* create = reinterpret_cast<const CREATESTRUCTA*>(lParam);
		self = static_cast<SdkWindow*>(create->lpCreateParams);
		self->m_hWnd = window;
		::SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	if (!self) return ::DefWindowProcA(window, message, wParam, lParam);

	LRESULT result = 0;
	try
	{
		result = self->WindowMessage(message, wParam, lParam);
	}
	catch (const std::exception& error)
	{
		IW::Logging::Error("Window message %u: %hs", message, error.what());
		if (message == WM_NCCREATE || message == WM_CREATE)
			result = message == WM_CREATE ? -1 : FALSE;
		else
			result = ::DefWindowProcA(window, message, wParam, lParam);
	}
	if (message == WM_NCDESTROY || (message == WM_NCCREATE && !result))
	{
		::SetWindowLongPtrA(window, GWLP_USERDATA, 0);
		self->m_hWnd = nullptr;
	}
	return result;
}
