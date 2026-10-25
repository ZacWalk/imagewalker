#pragma once

namespace IW::Test::UnwindChecks
{
	struct Trace
	{
		int order[4]{};
		int count = 0;
		void Record(int value) noexcept
		{
			if (count < 4) order[count] = value;
			++count;
		}
	};

	struct Probe
	{
		Trace &trace;
		int id;
		~Probe() noexcept { trace.Record(id); }
	};

	struct Signal {};

	// Retain real intervening stack frames in Release, where an inlined throw
	// could otherwise let the optimiser replace unwinding with ordinary stores.
	__declspec(noinline) inline void Throw()
	{
		throw Signal{};
	}

	__declspec(noinline) inline void CrossFrame(Trace &trace)
	{
		Probe first{trace, 1};
		{
			Probe second{trace, 2};
			Throw();
		}
	}
}

IW_TEST(CppExceptionsDestroyNestedRaiiObjectsAcrossStackFrames)
{
	IW::Test::UnwindChecks::Trace trace;
	bool caught = false;
	try
	{
		IW::Test::UnwindChecks::Probe outer{trace, 3};
		IW::Test::UnwindChecks::CrossFrame(trace);
	}
	catch (const IW::Test::UnwindChecks::Signal &)
	{
		caught = true;
		trace.Record(4);
	}
	IW_CHECK(caught);
	IW_CHECK_EQ(trace.count, 4);
	IW_CHECK_EQ(trace.order[0], 2);
	IW_CHECK_EQ(trace.order[1], 1);
	IW_CHECK_EQ(trace.order[2], 3);
	IW_CHECK_EQ(trace.order[3], 4);
}
