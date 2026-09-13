#pragma once

// Fixed layout for the band of chrome across the top of a frame.
//
// Every release did this with a rebar, which brought user-draggable bands,
// persisted band state, chevrons and a paint model where the rebar drew behind
// its children. The bands were never meant to be rearranged, the persisted
// state outlived the layout that produced it, and the paint model is the reason
// a command bar goes undrawn the moment you take the rebar away.
//
// The replacement is an ordered list of rows. A row holds items that are laid
// out left to right, right to left, or given whatever is left over. When a row
// runs out of width, items are dropped in ascending priority order rather than
// being scrolled behind a chevron -- every toolbar command is also on the menu,
// so hiding one costs nothing, and the item that must survive longest is simply
// given the highest priority.
//
// Sizes are asked of the controls. A toolbar is measured with TB_GETBUTTONSIZE
// and TB_GETMAXSIZE, a combo with CB_GETITEMHEIGHT, because the height passed to
// a drop-down combo at Create is its *dropped* height and says nothing about the
// closed one. Hard-coded band metrics are what broke these layouts above 100%
// DPI.

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <vector>

namespace IW
{
	class CChromeLayout
	{
	public:
		enum Kind
		{
			Toolbar, // measured with TB_GETBUTTONSIZE / TB_GETMAXSIZE
			Combo,   // measured with CB_GETITEMHEIGHT; needs a dropped height
			Custom   // caller supplies the width; height follows the row
		};

		enum Align
		{
			Left,
			Right,
			Stretch // takes whatever the row has left; never dropped
		};

		void Clear()
		{
			_items.clear();
		}

		// nPriority: lower is dropped first when the row runs out of width.
		// cx is only consulted for Custom items.
		//
		// The list is meant to be rebuilt on every layout pass. Building it once
		// and keeping it is a trap: a frame gets its first WM_SIZE part way
		// through OnCreate -- SetMenu alone is enough to cause one -- so a cached
		// list captures null handles for everything not yet created and never
		// lays those out again.
		void Add(int nRow, HWND hWnd, Kind kind, Align align, int nPriority, bool bWanted = true, int cx = 0)
		{
			if (hWnd == nullptr || !::IsWindow(hWnd))
				return;

			Item item = {};
			item.nRow = nRow;
			item.hWnd = hWnd;
			item.kind = kind;
			item.align = align;
			item.nPriority = nPriority;
			item.cx = cx;
			item.bWanted = bWanted;
			_items.push_back(item);
		}

		// Spacing, in 96dpi units; scaled to the actual DPI when laid out.
		// cxEdge/cyEdge pad the outside of the chrome block, cxGap separates
		// items in a row, cyGap separates the rows.
		void SetPadding(int cxEdge, int cyEdge, int cxGap, int cyGap)
		{
			_cxEdge = cxEdge;
			_cyEdge = cyEdge;
			_cxGap = cxGap;
			_cyGap = cyGap;
		}

		// Lays the rows out from the top of rect, then shrinks rect to what is
		// left for the client.
		void Layout(RECT& rect)
		{
			if (_items.empty())
				return;

			int nMaxRow = 0;
			for (const Item& item : _items)
				nMaxRow = (std::max)(nMaxRow, item.nRow);

			rect.top += Scale(_cyEdge);

			for (int nRow = 0; nRow <= nMaxRow; nRow++)
				LayoutRow(nRow, rect);

			rect.top += Scale(_cyEdge);
		}

	private:
		struct Item
		{
			int nRow;
			HWND hWnd;
			Kind kind;
			Align align;
			int nPriority;
			int cx;
			bool bWanted;
		};

		static int MeasureHeight(const Item& item)
		{
			if (item.hWnd == nullptr)
				return 0;

			if (item.kind == Toolbar)
			{
				const DWORD dwSize = static_cast<DWORD>(::SendMessage(item.hWnd, TB_GETBUTTONSIZE, 0, 0));
				const int cy = HIWORD(dwSize);
				return (cy > 0) ? cy + 2 : 22;
			}

			if (item.kind == Combo)
			{
				int cy = static_cast<int>(::SendMessage(item.hWnd, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0));
				if (cy <= 0)
					cy = 16;

				return cy + 2 * ::GetSystemMetrics(SM_CYEDGE) + 2;
			}

			RECT rc = {};
			::GetWindowRect(item.hWnd, &rc);
			return rc.bottom - rc.top;
		}

		static int MeasureWidth(const Item& item, int cyRow)
		{
			if (item.hWnd == nullptr)
				return 0;

			if (item.kind == Toolbar)
			{
				SIZE size = {0, 0};
				if (::SendMessage(item.hWnd, TB_GETMAXSIZE, 0, (LPARAM)&size) && size.cx > 0)
					return size.cx;
			}

			// Proportional to the row so it tracks the DPI rather than a literal.
			return (item.cx > 0) ? item.cx : cyRow * 4;
		}

		void LayoutRow(int nRow, RECT& rect)
		{
			std::vector<Item*> row;
			for (Item& item : _items)
			{
				if (item.nRow == nRow && item.hWnd != nullptr)
					row.push_back(&item);
			}

			if (row.empty())
				return;

			int cyRow = 0;
			for (const Item* item : row)
			{
				if (item->bWanted)
					cyRow = (std::max)(cyRow, MeasureHeight(*item));
			}

			// A row where nothing is wanted still has to put its windows away.
			// Returning first left them on screen at their last position, over
			// the top of whatever the frame placed beneath the chrome.
			if (cyRow <= 0)
			{
				for (const Item* item : row)
					Hide(item->hWnd);

				return;
			}

			const int cxEdge = Scale(_cxEdge);
			const int cxGap = Scale(_cxGap);
			const int cxRow = (rect.right - rect.left) - 2 * cxEdge;

			// Decide what fits. Stretch items are never dropped, only squeezed.
			std::vector<Item*> shown;
			for (Item* item : row)
			{
				if (item->bWanted)
					shown.push_back(item);
			}

			auto required = [&]()
			{
				int cx = 0;
				for (const Item* item : shown)
					cx += ((item->align == Stretch) ? cyRow * 6 : MeasureWidth(*item, cyRow)) + cxGap;
				return (cx > 0) ? cx - cxGap : 0;
			};

			while (required() > cxRow)
			{
				// Drop the lowest priority item that is allowed to go.
				auto it = shown.end();
				for (auto i = shown.begin(); i != shown.end(); ++i)
				{
					if ((*i)->align == Stretch)
						continue;

					if (it == shown.end() || (*i)->nPriority < (*it)->nPriority)
						it = i;
				}

				if (it == shown.end())
					break; // only stretch items left; let them squeeze

				shown.erase(it);
			}

			int xLeft = rect.left + cxEdge;
			int xRight = rect.right - cxEdge;

			// Right-aligned items first, so the stretch item knows its budget.
			for (auto i = row.rbegin(); i != row.rend(); ++i)
			{
				Item* item = *i;
				if (item->align != Right)
					continue;

				if (!Contains(shown, item))
				{
					Hide(item->hWnd);
					continue;
				}

				const int cx = MeasureWidth(*item, cyRow);
				xRight -= cx;
				Place(item, xRight, rect.top, cx, cyRow);
				xRight -= cxGap;
			}

			for (Item* item : row)
			{
				if (item->align != Left)
					continue;

				if (!Contains(shown, item))
				{
					Hide(item->hWnd);
					continue;
				}

				const int cx = MeasureWidth(*item, cyRow);
				Place(item, xLeft, rect.top, cx, cyRow);
				xLeft += cx + cxGap;
			}

			for (Item* item : row)
			{
				if (item->align != Stretch)
					continue;

				if (!Contains(shown, item))
				{
					Hide(item->hWnd);
					continue;
				}

				Place(item, xLeft, rect.top, (xRight > xLeft) ? xRight - xLeft : 0, cyRow);
			}

			if (!shown.empty())
				rect.top += cyRow + Scale(_cyGap);
		}

		// Padding is specified at 96dpi and scaled, so it stays visually the
		// same size rather than shrinking as the display gets denser.
		static int Scale(int n)
		{
			static int nDpi = 0;

			if (nDpi == 0)
			{
				const HDC hdc = ::GetDC(nullptr);
				nDpi = (hdc != nullptr) ? ::GetDeviceCaps(hdc, LOGPIXELSX) : 96;
				if (hdc != nullptr)
					::ReleaseDC(nullptr, hdc);

				if (nDpi <= 0)
					nDpi = 96;
			}

			return ::MulDiv(n, nDpi, 96);
		}

		static bool Contains(const std::vector<Item*>& v, const Item* item)
		{
			return std::find(v.begin(), v.end(), item) != v.end();
		}

		static void Hide(HWND hWnd)
		{
			::SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
			               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_HIDEWINDOW);
		}

		static void Place(const Item* item, int x, int y, int cx, int cy)
		{
			// A drop-down combo keeps the height argument as its dropped height
			// and picks its own closed height, so ask for a generous one.
			const int cyPlace = (item->kind == Combo) ? cy + 200 : cy;

			::SetWindowPos(item->hWnd, nullptr, x, y, cx, cyPlace,
			               SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		}

		std::vector<Item> _items;

		int _cxEdge = 4;
		int _cyEdge = 3;
		int _cxGap = 4;
		int _cyGap = 3;
	};
}
