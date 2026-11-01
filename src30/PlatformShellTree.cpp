// ImageWalker by Zac Walker
// Adapts the shared libdesktop shell tree and drag/drop contracts.

#include "DesktopServices.h"
#include "Platform.h"

namespace iw::platform
{
	namespace
	{
		std::filesystem::path path_from_pf(const pf::file_path& path)
		{
			return std::filesystem::path(pf::utf8_to_utf16(path.view()));
		}

		pf::file_path path_to_pf(const std::filesystem::path& path)
		{
			return pf::file_path(desktop::utf8_path(path));
		}

		class PfShellTree final : public ShellTree
		{
		public:
			PfShellTree(const WindowFramePtr& owner, ShellTreeOptions options)
				: owner_(owner), options_(std::move(options))
			{
				const auto frame = native_frame(owner_);
				if (!frame) return;
				pf::shell_tree_options shared;
				shared.selection_changed = [this](const pf::file_path path)
				{
					if (options_.selectionChanged) options_.selectionChanged(path_from_pf(path));
				};
				shared.files_changed = [this]
				{
					if (options_.filesChanged) options_.filesChanged();
				};
				shared.files_dropped = [this](const pf::file_path target,
				                              const std::vector<pf::file_path> files,
				                              const bool move)
				{
					std::vector<std::filesystem::path> sources;
					sources.reserve(files.size());
					for (const auto& file : files) sources.push_back(path_from_pf(file));
					perform_file_operation(owner_, move ? FileOperation::move : FileOperation::copy,
					                      sources, path_from_pf(target));
				};
				tree_ = pf::create_shell_tree(frame, std::move(shared));
			}

			void set_bounds(const app::recti bounds) override
			{
				if (tree_) tree_->set_bounds({bounds.x, bounds.y, bounds.right(), bounds.bottom()});
			}

			void show(const bool visible) override { if (tree_) tree_->show(visible); }
			void set_focus() override { if (tree_) tree_->set_focus(); }
			bool has_focus() const override { return tree_ && tree_->has_focus(); }
			void set_current_path(const std::filesystem::path& path) override
			{
				if (tree_) tree_->set_current_path(path_to_pf(path));
			}
			void refresh() override { if (tree_) tree_->refresh(); }

		private:
			WindowFramePtr owner_;
			ShellTreeOptions options_;
			pf::shell_tree_ptr tree_;
		};
	}

	ShellTreePtr create_shell_tree(const WindowFramePtr& owner, ShellTreeOptions options)
	{
		if (!owner || !native_frame(owner)) return {};
		return std::make_shared<PfShellTree>(owner, std::move(options));
	}

	bool begin_file_drag(const WindowFramePtr& owner,
	                     const std::span<const std::filesystem::path> paths)
	{
		const auto frame = native_frame(owner);
		if (!frame || paths.empty()) return false;
		std::vector<pf::file_path> files;
		files.reserve(paths.size());
		for (const auto& path : paths) files.push_back(path_to_pf(path));
		return pf::begin_file_drag(frame, files);
	}
}
