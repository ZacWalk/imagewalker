#pragma once

#include "channelaverage.h"

IW_TEST(ChannelAveragesPreserveEveryByteAndNeverWrap)
{
	for (unsigned value = 0; value <= 255; ++value)
	{
		for (unsigned count : {1u, 2u, 255u, 65535u, 16843009u})
		{
			IW_CHECK_EQ(IW::ChannelAverage(value * count, count), value);
			IW_CHECK_EQ(IW::ChannelAverage(value * count, count, true), value);
		}
	}
	IW_CHECK_EQ(IW::ChannelAverage(0, 0), 0);
	IW_CHECK_EQ(IW::ChannelAverage(255, 0), 0);
	IW_CHECK_EQ(IW::ChannelAverage(1, 2), 0);
	IW_CHECK_EQ(IW::ChannelAverage(1, 2, true), 1);
	IW_CHECK_EQ(IW::ChannelAverage(0xffffffffu, 16843009u, true), 255);
	IW_CHECK_EQ(IW::ChannelAverage(256, 1), 255);
}
