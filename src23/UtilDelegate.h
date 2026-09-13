// ImageWalker by Zac Walker
//
// Purpose: A notification list. Bindings are member functions on objects
//          that outlive the list, which is why there is no unbind.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

#pragma once

namespace Delegate
{
	// A notification list. Every binding is a member function on an object that
	// outlives the list, which is why there is no unbind.
	class List0
	{
	private:
		std::vector<std::function<void()>> _delegates;

	public:
		template<class T>
		void Bind(T *pT, void (T::*func)())
		{
			_delegates.push_back([pT, func] { (pT->*func)(); });
		}

		void Invoke() const
		{
			for (const auto &d : _delegates)
				d();
		}
	};
}


