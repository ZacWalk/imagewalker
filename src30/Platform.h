// ImageWalker by Zac Walker
// Defines OS-neutral windows, command surfaces, shell navigation, drawing, and runtime services.

#pragma once

#ifndef RC_INVOKED
#include "util_color.h"
#include "util_geometry.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace iw::platform
{
	// Opaque OS handle. Only platform backends may interpret it; application code must not.
	using NativeHandle = std::uintptr_t;
	using MessageResult = std::intptr_t;

	struct BitmapResource
	{
		sizei size;
		std::vector<std::uint32_t> pixels;
		bool hasAlpha{};
	};

	enum class CursorShape { arrow, zoom, sizeAll, sizeHorizontal, wait };

	enum class SystemColor
	{
		window,
		windowText,
		face,
		shadow,
		highlight,
		highlightText,
		grayText,
		buttonText,
		scrollbar
	};

	enum class WindowMessage
	{
		create,
		destroy,
		close,
		dpiChanged,
		focusGained,
		focusLost,
		captureLost,
		timer
	};
	enum class MouseMessage
	{
		leftButtonDown,
		leftButtonDoubleClick,
		rightButtonDown,
		leftButtonUp,
		move,
		wheel,
		leave,
		contextMenu
	};

	enum class KeyMessage { down, character };

	enum class KeyCode
	{
		unknown,
		left,
		right,
		up,
		down,
		home,
		end,
		pageUp,
		pageDown,
		space,
		deleteKey,
		f2,
		enter,
		escape,
		divide,
		multiply,
		add,
		subtract
	};

	enum class GestureKind { zoom, pan };

	struct MouseInput
	{
		pointi point;
		pointi screenPoint;
		bool leftButton{};
		bool control{};
		bool shift{};
		int wheelDelta{};
	};

	struct KeyInput
	{
		KeyCode key{KeyCode::unknown};
		wchar_t character{};
		bool control{};
		bool shift{};
		bool alt{};
	};

	struct GestureInput
	{
		GestureKind kind{};
		pointi point;
		std::uint64_t argument{};
		bool begin{};
		bool end{};
	};

	struct FileDrop
	{
		std::vector<std::filesystem::path> files;
		pointi point;
	};

	namespace TextFormat
	{
		inline constexpr std::uint32_t left = 1u << 0;
		inline constexpr std::uint32_t center = 1u << 1;
		inline constexpr std::uint32_t right = 1u << 2;
		inline constexpr std::uint32_t verticalCenter = 1u << 3;
		inline constexpr std::uint32_t singleLine = 1u << 4;
		inline constexpr std::uint32_t endEllipsis = 1u << 5;
	}

	class Font
	{
	public:
		Font();
		~Font();
		Font(Font&&) noexcept;
		Font& operator=(Font&&) noexcept;
		Font(const Font&) = delete;
		Font& operator=(const Font&) = delete;

		explicit operator bool() const;
		NativeHandle native_handle() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
		explicit Font(std::unique_ptr<Impl> impl);
		friend class DrawContext;
		friend std::shared_ptr<Font> create_message_font(unsigned int dpi);
	};

	using FontPtr = std::shared_ptr<Font>;

	class DrawContext
	{
	public:
		virtual ~DrawContext() = default;
		virtual recti clip_rect() const = 0;
		virtual void fill(recti rect, color value) = 0;
		virtual void blend_fill(recti rect, color value) = 0;
		virtual void text(std::wstring_view value, recti rect, color textColor,
		                  std::uint32_t format, const FontPtr& font = {}) = 0;
		virtual sizei measure_text(std::wstring_view value, std::uint32_t format,
		                           const FontPtr& font = {}) = 0;
		virtual void image(std::span<const std::uint32_t> pixels, sizei source,
		                   recti destination, bool alpha) = 0;
		virtual void focus(recti rect) = 0;
		virtual void outline(recti rect, color value, int width = 1) = 0;
		virtual void clip(recti rect) = 0;
		virtual void reset_clip() = 0;
	};

	class MeasureContext
	{
	public:
		virtual ~MeasureContext() = default;
		virtual sizei measure_text(std::wstring_view value, std::uint32_t format,
		                           const FontPtr& font = {}) const = 0;
	};

	class WindowFrame;
	class FrameReactor;
	using WindowFramePtr = std::shared_ptr<WindowFrame>;
	using FrameReactorPtr = std::shared_ptr<FrameReactor>;
	enum class WindowIcon { none, application };

	struct WindowOptions
	{
		std::string className;
		std::wstring title;
		recti bounds;
		bool child{};
		bool visible{true};
		bool clipChildren{true};
		bool eraseBackground{true};
		bool tabStop{};
		int showCommand{};
		WindowIcon icon{WindowIcon::none};
		bool useDefaultPosition{};
		bool maximized{};
	};

	struct WindowPlacement
	{
		recti normalBounds;
		bool maximized{};
	};

	enum class ShortcutKey
	{
		none,
		a,
		b,
		c,
		e,
		n,
		o,
		r,
		v,
		x,
		z,
		deleteKey,
		enterKey,
		f1,
		f2,
		f3,
		f4,
		f5,
		f6,
		f7,
		f8,
		f9,
		f11,
		f12
	};

	struct Shortcut
	{
		ShortcutKey key{ShortcutKey::none};
		bool control{};
		bool shift{};
		bool alt{};
	};

	struct CommandAccelerator
	{
		Shortcut shortcut;
		std::uint16_t command{};
	};

	class WindowFrame
	{
	public:
		virtual ~WindowFrame() = default;
		virtual NativeHandle native_handle() const = 0;
		virtual void set_reactor(FrameReactorPtr reactor) = 0;
		virtual WindowFramePtr create_child(FrameReactorPtr reactor, const WindowOptions& options) = 0;
		virtual recti client_rect() const = 0;
		virtual void move(recti bounds) = 0;
		virtual void show(bool visible) = 0;
		virtual void invalidate() = 0;
		virtual void invalidate(recti bounds) = 0;
		virtual void set_focus() = 0;
		virtual bool has_focus() const = 0;
		virtual void set_capture() = 0;
		virtual void release_capture() = 0;
		virtual void track_mouse_leave() = 0;
		virtual void configure_gestures(bool zoom, bool pan) = 0;
		virtual void start_timer(unsigned int milliseconds) = 0;
		virtual void stop_timer() = 0;
		virtual void accept_file_drops(bool accept = true) = 0;
		virtual void set_cursor(CursorShape cursor) = 0;
		virtual pointi screen_to_client(pointi point) const = 0;
		virtual pointi client_to_screen(pointi point) const = 0;
		virtual void set_title(std::wstring_view title) = 0;
		virtual void set_fullscreen(bool fullscreen) = 0;
		virtual void set_maximized(bool maximized) = 0;
		virtual WindowPlacement placement() const = 0;
		virtual void set_accelerators(std::span<const CommandAccelerator> accelerators) = 0;
		virtual void close() = 0;
		virtual unsigned int dpi() const = 0;
	};

	class FrameReactor
	{
	public:
		virtual ~FrameReactor() = default;

		virtual MessageResult message(const WindowFramePtr&, WindowMessage) { return 0; }
		virtual MessageResult mouse(const WindowFramePtr&, MouseMessage, const MouseInput&) { return 0; }
		virtual MessageResult key(const WindowFramePtr&, KeyMessage, const KeyInput&) { return 0; }
		virtual MessageResult gesture(const WindowFramePtr&, const GestureInput&) { return 0; }
		virtual MessageResult files_dropped(const WindowFramePtr&, const FileDrop&) { return 0; }

		virtual void paint(const WindowFramePtr&, DrawContext&)
		{
		}

		virtual void size(const WindowFramePtr&, sizei, MeasureContext&)
		{
		}
	};

	WindowFramePtr create_top_level_frame(FrameReactorPtr reactor, const WindowOptions& options);
	FontPtr create_message_font(unsigned int dpi);
	enum class BitmapAsset
	{
		fileFolder, filePhoto, fileDocument, fileVideo, fileAudio, fileOther,
		toolbarSymbols, statusSymbols
	};
	BitmapResource load_bitmap_resource(BitmapAsset asset);
	color system_color(SystemColor value);
	color calc_hande_color(bool hover, bool selected, bool checked);
	sizei drag_threshold();

	enum class CommandBitmap
	{
		none = -1,
		forward,
		back,
		parent,
		refresh,
		folderTree,
		openFolder,
		about,
		details,
		thumbnails,
		matrix,
		drive,
		photosOnly,
		sort,
		breadcrumbChevron
	};

	struct Command
	{
		CommandBitmap bitmap{CommandBitmap::none};
		std::wstring name;
		Shortcut shortcut;
		std::wstring tooltip;
		std::function<std::wstring()> toolbarText;
		std::function<void()> invoke;
		std::function<bool()> enabled;
		std::function<bool()> checked;
	};
	using CommandPtr = std::shared_ptr<Command>;

	enum class MenuItemKind { command, submenu, separator };

	struct MenuItem
	{
		MenuItemKind kind{MenuItemKind::command};
		CommandPtr command;
		std::wstring name;
		std::vector<MenuItem> children;

		static MenuItem action(CommandPtr command);
		static MenuItem submenu(std::wstring label, std::vector<MenuItem> items);
		static MenuItem separator();
	};

	struct Menu
	{
		std::wstring name;
		std::vector<MenuItem> items;
	};

	struct PopupItem
	{
		CommandPtr command;
		std::wstring label;
		bool separator{};

		static PopupItem action(CommandPtr command, std::wstring text);
		static PopupItem separator_item();
	};

	void show_popup_menu(const WindowFramePtr& owner, pointi screenPoint, std::span<const PopupItem> items);

	enum class ToolbarItemKind { command, menu, separator };

	struct ToolbarItem
	{
		ToolbarItemKind kind{ToolbarItemKind::command};
		CommandPtr command;
		std::vector<MenuItem> menuItems;

		static ToolbarItem action(CommandPtr command);
		static ToolbarItem menu(CommandPtr command, std::vector<MenuItem> items);
		static ToolbarItem separator();
	};

	struct Breadcrumb
	{
		std::filesystem::path path;
		std::wstring label;
	};

	struct StatusInfo
	{
		std::wstring text;
		int progressPercent{100};
		bool scanning{};
	};

	class CommandSurface
	{
	public:
		virtual ~CommandSurface() = default;
		virtual void set_menu(std::vector<Menu> menus) = 0;
		virtual void set_toolbar(std::vector<ToolbarItem> items) = 0;
		// A task view owns the top toolbar while it is open; clearing restores the browsing one.
		virtual void set_task_toolbar(std::vector<ToolbarItem> items) = 0;
		virtual void clear_task_toolbar() = 0;
		virtual void set_status_toolbar(std::vector<ToolbarItem> items) = 0;
		virtual void set_breadcrumbs(std::vector<Breadcrumb> breadcrumbs,
		                             std::function<void(const std::filesystem::path&)> selected) = 0;
		virtual void set_status(StatusInfo status) = 0;
		virtual void set_thumbnail_size(int value, std::function<void(int)> changed) = 0;
		virtual recti layout(recti client, bool fullscreen) = 0;
		virtual void set_dpi(unsigned int dpi) = 0;
		virtual void refresh() = 0;
	};

	using CommandSurfacePtr = std::shared_ptr<CommandSurface>;
	CommandSurfacePtr create_command_surface(const WindowFramePtr& owner);

	struct ShellTreeOptions
	{
		std::function<void(const std::filesystem::path&)> selectionChanged;
		std::function<void()> filesChanged;
	};

	class ShellTree
	{
	public:
		virtual ~ShellTree() = default;
		virtual void set_bounds(recti bounds) = 0;
		virtual void show(bool visible) = 0;
		virtual void set_focus() = 0;
		virtual bool has_focus() const = 0;
		virtual void set_current_path(const std::filesystem::path& path) = 0;
		virtual void refresh() = 0;
	};

	using ShellTreePtr = std::shared_ptr<ShellTree>;
	ShellTreePtr create_shell_tree(const WindowFramePtr& owner, ShellTreeOptions options);
	bool begin_file_drag(const WindowFramePtr& owner, std::span<const std::filesystem::path> paths);

	using DialogFieldId = std::uint32_t;
	using DialogValue = std::variant<std::monostate, std::wstring, bool, std::int64_t>;
	using DialogValues = std::map<DialogFieldId, DialogValue>;

	enum class DialogFieldKind { text, checkBox, choice, action, searchList };

	struct DialogChoice
	{
		std::wstring label;
		std::int64_t value{};
	};

	struct DialogField
	{
		DialogFieldId id{};
		DialogFieldKind kind{DialogFieldKind::text};
		std::wstring label;
		DialogValue value;
		std::vector<DialogChoice> choices;
		std::function<void()> invoke;
		int selectionStart{};
		int selectionEnd{-1};
		bool enabled{true};
		bool folderCompletion{};
		std::function<void(DialogValues&)> editValues;
	};

	struct DialogDefinition
	{
		std::wstring title;
		std::wstring message;
		std::vector<DialogField> fields;
		std::wstring acceptText{L"OK"};
		std::wstring cancelText{L"Cancel"};
		std::function<std::optional<std::wstring>(const DialogValues&)> validate;
		std::vector<BitmapResource> previews;
	};

	std::optional<DialogValues> show_modal_dialog(const WindowFramePtr& owner, DialogDefinition definition);
	// Runs owned work on the task queue while a non-cancellable status window pumps UI messages.
	// Returns its result; worker exceptions are rethrown on the caller after completion.
	bool run_with_status(const WindowFramePtr& owner, std::wstring_view title, std::wstring_view message,
	                     std::function<bool()> work);

	struct ChoiceButton
	{
		int id{};
		std::wstring label;
	};

	// More than accept and cancel: "Save / Don't Save / Cancel" and the counted
	// permanent-delete confirmation both need a named third answer.
	struct ChoiceDefinition
	{
		std::wstring title;
		std::wstring heading;
		std::wstring message;
		std::vector<ChoiceButton> buttons;
		int defaultButton{};
		bool warning{};
	};

	// The chosen button id, or 0 when the dialog was dismissed without choosing.
	int show_choice(const WindowFramePtr& owner, const ChoiceDefinition& definition);

	struct TextInputOptions
	{
		std::wstring text;
		std::wstring cueBanner;
		bool folderCompletion{};
		std::function<void(const std::wstring&)> changed;
		std::function<void()> accepted;
		std::function<void()> escaped;
		std::function<void(bool backwards)> tabbed;
	};

	// A hosted native edit control. Drawing a caret and a selection is not worth writing.
	class TextInput
	{
	public:
		virtual ~TextInput() = default;
		virtual void set_bounds(recti bounds) = 0;
		virtual void show(bool visible) = 0;
		virtual void set_focus() = 0;
		virtual bool has_focus() const = 0;
		virtual std::wstring text() const = 0;
		virtual void set_text(std::wstring_view value) = 0;
		virtual void set_font(const FontPtr& font) = 0;
	};

	using TextInputPtr = std::shared_ptr<TextInput>;
	TextInputPtr create_text_input(const WindowFramePtr& owner, TextInputOptions options);

	class AppHandler
	{
	public:
		virtual ~AppHandler() = default;
		virtual const std::wstring& name() const = 0;
		virtual bool invoke(std::span<const std::filesystem::path> paths) const = 0;
	};

	using AppHandlerPtr = std::shared_ptr<AppHandler>;
	std::vector<AppHandlerPtr> registered_apps_for_extension(std::wstring_view extension);

	// Launches an already-discovered executable. The program path is never parsed out of
	// `arguments`, so configuration text cannot become the thing that runs.
	bool launch_process(const std::filesystem::path& executable, std::wstring_view arguments,
	                    const WindowFramePtr& owner);

	void open_uri(std::wstring_view uri);

	// Background queues. Each one runs its items one at a time, in submission order, on a
	// dedicated thread, so items sharing a queue never run concurrently with each other.
	// `task` is reserved for multi-minute batch work, which must not sit in front of folder scanning.
	enum class WorkQueue { image, thumbnail, metadata, folder, shell, task };

	// Runs work on the UI thread. Callable from any thread; false means the UI loop has ended.
	bool queue_ui(std::function<void()> work);

	// Runs work on a background queue. Callable from any thread; false means the queues are stopped.
	bool queue_work(WorkQueue queue, std::function<void()> work);

	int compare_ordinal_ignore_case(std::wstring_view left, std::wstring_view right);
	int compare_file_names(std::wstring_view left, std::wstring_view right);

	struct SaveFileOptions
	{
		std::wstring_view filterName;
		std::wstring_view filterPattern;
		std::wstring_view defaultExtension;
		std::wstring_view initialName;
	};

	struct OpenFileOptions
	{
		std::wstring_view filterName;
		std::wstring_view filterPattern;
	};

	std::optional<std::filesystem::path> choose_folder(const WindowFramePtr& owner, std::wstring_view title);
	std::optional<std::filesystem::path> choose_open_file(const WindowFramePtr& owner, const OpenFileOptions& options);
	std::optional<std::filesystem::path> choose_save_file(const WindowFramePtr& owner, const SaveFileOptions& options);

	struct IntegerSetting
	{
		std::wstring key;
		int value{};
	};

	int read_integer_setting(std::wstring_view section, std::wstring_view key, int fallback);
	void write_integer_settings(std::wstring_view section, std::span<const IntegerSetting> values);

	struct TextSetting
	{
		std::wstring key;
		std::wstring value;
	};

	std::wstring read_text_setting(std::wstring_view section, std::wstring_view key,
	                               std::wstring_view fallback = {});
	void write_text_settings(std::wstring_view section, std::span<const TextSetting> values);
	void clear_settings_section(std::wstring_view section);

	enum class KnownFolder { pictures, documents, desktop, downloads, music, videos };

	// An empty path means the folder is not present on this machine.
	std::filesystem::path known_folder(KnownFolder folder);

	// Settings and configuration live beside the executable, never in the registry.
	std::filesystem::path module_folder();

	enum class FileOperation { copy, move, recycle };

	struct ClipboardStatus
	{
		bool hasFiles{};
		bool hasImage{};
	};

	struct ClipboardContent
	{
		std::vector<std::filesystem::path> files;
		BitmapResource image;
	};

	ClipboardStatus clipboard_status();
	bool set_clipboard_files(const WindowFramePtr& owner, std::span<const std::filesystem::path> paths, bool move);
	ClipboardContent read_clipboard(const WindowFramePtr& owner);
	bool perform_file_operation(const WindowFramePtr& owner, FileOperation operation,
	                            std::span<const std::filesystem::path> sources,
	                            const std::filesystem::path& destination = {});

	// No Recycle Bin, and a network path may have no recoverable bin at all. Kept separate
	// from FileOperation::recycle so a caller cannot reach it by passing the wrong enumerator.
	bool delete_permanently(std::span<const std::filesystem::path> paths, std::error_code& error);

	enum class RuntimeMode { multithreaded, userInterface };

	class Runtime
	{
	public:
		explicit Runtime(RuntimeMode mode);
		~Runtime();
		Runtime(const Runtime&) = delete;
		Runtime& operator=(const Runtime&) = delete;

		explicit operator bool() const { return initialized_; }

	private:
		RuntimeMode mode_;
		bool initialized_{};
	};

	void show_error(std::wstring_view message, std::wstring_view title, const WindowFramePtr& owner = {});
	bool show_help(const WindowFramePtr& owner = {});

	// Writes to the latest-run UTF-8 log, attached console and debugger.
	void write_diagnostic(std::wstring_view text);

	// A temporary folder name that no concurrently running instance will pick.
	std::filesystem::path unique_temp_folder(std::wstring_view name);

	struct FileAttributes
	{
		bool known{};
		bool hidden{};
		bool directory{};
		bool reparse{};
	};

	FileAttributes read_file_attributes(const std::filesystem::path& path);

	// x64 always has SSE2; on x86 it is a runtime fact.
	bool has_sse2();
	// AVX2 also requires operating-system support for saving the extended vector state.
	bool has_avx2();

	// Pumps messages until the window closes, then stops the background queues and joins their threads.
	int run_ui_loop(const WindowFramePtr& window);
}

int imagewalker_main(int showCommand, std::span<const std::wstring_view> arguments);
#endif
