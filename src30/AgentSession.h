// ImageWalker by Zac Walker
// The fixed set of actions an MCP client may perform on a disposable fixture.

#pragma once

#include "AgentPlatform.h"

#include <libdesktop/core/mcp.h>

namespace iw
{
	class MainWindow;

	class AgentSession
	{
	public:
		explicit AgentSession(std::unique_ptr<platform::agent::Fixture> fixture, bool captureAllowed);
		const ::core::mcp::server_ptr& server() const { return server_; }
		bool start(const std::shared_ptr<MainWindow>& window);
		void stop();

	private:
		class Frame;

		void dispatch(MainWindow& window, const ::core::mcp::call_ptr& call);
		::core::mcp::tool_result state(const MainWindow& window) const;
		bool browse(MainWindow& window, std::string folder);
		::core::mcp::tool_result select_photos(MainWindow& window, const std::vector<std::string>& names);
		::core::mcp::tool_result open_preview(MainWindow& window, std::string_view view);
		::core::mcp::tool_result close_view(MainWindow& window) const;
		::core::mcp::tool_result fullscreen(MainWindow& window, bool enabled) const;
		static void screenshot(const MainWindow& window, const ::core::mcp::call_ptr& call);

		std::unique_ptr<platform::agent::Fixture> fixture_;
		::core::mcp::server_ptr server_;
		std::weak_ptr<MainWindow> window_;
		std::string folder_;
		size_t scanComplete_{};
		size_t scanTotal_{};
		size_t scanPhotos_{};
		size_t thumbnailsComplete_{};
		size_t thumbnailsTotal_{};
		bool imageFailed_{};
		bool captureAllowed_{};
	};
}
