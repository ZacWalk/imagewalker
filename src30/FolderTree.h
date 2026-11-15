// ImageWalker by Zac Walker
// Declares the folder tree ImageWalker 3.0 draws itself: its lazily listed model and the control that shows it.

#pragma once

#include <libdesktop/ui.h>
#include <libdesktop/ui/views/view_base.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace iw::folders
{
	// A folder the tree shows: a drive at the top, or a subfolder listed when its parent first opens.
	struct Folder
	{
		std::filesystem::path path;
		std::wstring label;
		bool drive{};
	};

	// The folders a tree shows and which of them is selected. A folder's subfolders are unknown until it is
	// first expanded: the model then asks for them and shows the folder expanding until they arrive. Nothing
	// here touches the file system.
	class FolderTreeModel
	{
	public:
		struct Row
		{
			const Folder* folder{};
			int depth{};
			// Its subfolders are unknown, or there are some.
			bool expandable{};
			bool expanded{};
			// Its subfolders were asked for and have not arrived.
			bool listing{};
		};

		using ListRequest = std::function<void(const std::filesystem::path&)>;
		// The folders' storage, private to the model's implementation.
		struct Node;

		explicit FolderTreeModel(ListRequest request = {});
		~FolderTreeModel();
		FolderTreeModel(const FolderTreeModel&) = delete;
		FolderTreeModel& operator=(const FolderTreeModel&) = delete;

		// Replaces the top level and collapses everything; the folder that was selected, or was being
		// revealed, is revealed again.
		void set_roots(std::vector<Folder> roots);
		// The subfolders a request asked for, in the order shown; a folder that could not be read has none.
		void listed(const std::filesystem::path& folder, std::vector<Folder> subfolders);

		const std::vector<Row>& rows() const { return rows_; }
		std::optional<size_t> selected_row() const;
		std::optional<std::filesystem::path> selected_path() const;

		// What the user does. Each stops a reveal in progress; select and move answer whether the
		// selection changed, expand, collapse and toggle whether the rows did. Collapsing a folder that
		// holds the selection selects it.
		bool select(size_t row);
		bool expand(size_t row);
		bool collapse(size_t row);
		bool toggle(size_t row);

		enum class Move
		{
			up,
			down,
			left,
			right,
			home,
			end,
			pageUp,
			pageDown
		};
		// As a native tree moves: Left collapses an open folder or goes to its parent, Right opens a closed
		// one or goes to its first subfolder. A page is `page` rows.
		bool move(Move move, size_t page = 1);

		// Opens the folders above path, listing them as needed, and selects it once it is shown. A folder
		// the tree cannot show - hidden, or under no root - leaves nothing selected.
		void reveal(const std::filesystem::path& path);
		bool revealing() const { return !reveal_.empty(); }

	private:
		void rebuild();
		void add_rows(Node& node, int depth);
		void open(Node& node);
		void continue_reveal();
		Node* find_listing(std::vector<std::unique_ptr<Node>>& nodes, const std::filesystem::path& folder) const;

		ListRequest request_;
		std::vector<std::unique_ptr<Node>> roots_;
		std::vector<Row> rows_;
		std::vector<Node*> rowNodes_;
		Node* selected_{};
		std::filesystem::path reveal_;
	};

	// The tree, drawn: a row for each shown folder with its expander and glyph, the selection and the row
	// under the pointer, scrolled by the wheel, the keys and its scroll bar. A click or a key that moves the
	// selection reports the folder chosen; one revealed from outside is selected without a report.
	class FolderTreeView final : public ::ui::view_base
	{
	public:
		struct Events
		{
			std::function<void(const std::filesystem::path&)> selected;
			// The subfolders of a folder are wanted, through listed().
			std::function<void(const std::filesystem::path&)> list;
		};

		explicit FolderTreeView(Events events);

		void set_roots(std::vector<Folder> roots);
		void listed(const std::filesystem::path& folder, std::vector<Folder> subfolders);
		void reveal(const std::filesystem::path& path);
		const FolderTreeModel& model() const { return model_; }

		// The folder drawn at loc, in the control's own coordinates.
		std::optional<std::filesystem::path> folder_at(core::pointi loc) const;
		int row_height() const { return rowHeight_; }

		void on_placed(render::measure_context& mc) override;
		void paint(render::draw_context& dc, bool focused) const override;
		bool on_key(char32_t key, ::platform::key_state keys) override;
		void on_mouse_down(core::pointi loc, ::platform::key_state keys) override;
		void on_mouse_move(core::pointi loc, bool captured) override;
		void on_double_click(core::pointi loc, ::platform::key_state keys) override;
		void on_mouse_leave() override;
		bool on_wheel(core::pointi loc, int delta, ::platform::key_state keys) override;
		void scale_changed() override;
		void dispatch_event(const ::ui::element_event& event) override;

	private:
		enum class Part
		{
			none,
			expander,
			folder
		};

		struct Hit
		{
			size_t row{};
			Part part{Part::none};
		};

		void update_metrics(render::measure_context& mc);
		void rows_changed();
		void ensure_visible(size_t row);
		Hit hit_test(core::pointi loc) const;
		void report_if_changed(const std::optional<std::filesystem::path>& before);
		const render::texture_ptr& glyph(render::draw_context& dc, bool drive) const;

		FolderTreeModel model_;
		Events events_;
		render::font_face font_{render::font_face::dialog};
		double scale_{1.0};
		int rowHeight_{20};
		int indent_{16};
		int iconSize_{16};
		int padding_{4};
		int hot_{-1};
		mutable render::texture_ptr folderGlyph_;
		mutable render::texture_ptr driveGlyph_;
	};
}
