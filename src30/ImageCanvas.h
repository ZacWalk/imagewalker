// ImageWalker by Zac Walker
// Declares the custom DIB-rendered image and file browsing surface.

#pragma once

#include "Platform.h"
#include "AppLayout.h"
#include "CanvasRenderer.h"
#include "Dpi.h"
#include "Files.h"
#include "LoadingModel.h"
#include "SelectionModel.h"
#include "ZoomModel.h"
#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace iw
{
	namespace tests { class ImageCanvasAccess; }

	class ImageCanvas final : public platform::FrameReactor
	{
		friend class tests::ImageCanvasAccess;
	public:
		enum class ItemMode { details, thumbnails, matrix, single };

		ImageCanvas();
		~ImageCanvas() override = default;
		ImageCanvas(const ImageCanvas&) = delete;
		ImageCanvas& operator=(const ImageCanvas&) = delete;

		bool create(const platform::WindowFramePtr& parent);
		bool load(const std::filesystem::path& path);
		void invalidate_file(const std::filesystem::path& path);
		void invalidate_all_files();
		// Records dimensions read by a background probe without disturbing selection or thumbnails.
		void set_item_dimensions(const std::filesystem::path& path, int width, int height);
		void set_files(std::vector<std::filesystem::path> files);
		void set_items(std::vector<files::FolderItem> items, size_t totalItemCount);
		void set_mode(ItemMode mode);
		void set_fullscreen(bool fullscreen);
		void set_file_properties_visible(bool visible);
		bool file_properties_visible() const { return propertiesExpanded_; }
		void set_thumbnail_selector_visible(bool visible);
		bool thumbnail_selector_visible() const { return thumbnailsExpanded_; }
		void set_sort(files::SortField field, bool ascending);
		void set_dpi(unsigned int dpi);
		void set_thumbnail_size(int size);
		int thumbnail_size() const { return thumbnailSize_; }

		void set_activation_handler(std::function<void(const std::filesystem::path&)> handler)
		{
			activationHandler_ = std::move(handler);
		}

		void set_selection_handler(std::function<void()> handler) { selectionHandler_ = std::move(handler); }
		void set_zoom_mode_handler(std::function<void(bool)> handler) { zoomModeHandler_ = std::move(handler); }
		struct CommandHandlers
		{
			std::function<void()> enterFullscreen;
			std::function<void()> leaveFullscreen;
			std::function<void()> toggleProperties;
			std::function<void()> toggleThumbnails;
		};
		void set_command_handlers(CommandHandlers handlers) { commandHandlers_ = std::move(handlers); }

		void set_context_menu_handler(std::function<void(app::pointi)> handler)
		{
			contextMenuHandler_ = std::move(handler);
		}

		void set_drop_handler(std::function<void(const std::vector<std::filesystem::path>&)> handler)
		{
			dropHandler_ = std::move(handler);
		}

		void set_clear_filter_handler(std::function<void()> handler) { clearFilterHandler_ = std::move(handler); }

		void set_sort_handler(std::function<void(files::SortField, bool)> handler)
		{
			sortHandler_ = std::move(handler);
		}

		void set_progress_handler(std::function<void(size_t, size_t)> handler)
		{
			progressHandler_ = std::move(handler);
		}

		void set_load_error_handler(std::function<void(const std::filesystem::path&)> handler)
		{
			loadErrorHandler_ = std::move(handler);
		}

		void set_information_progress_handler(std::function<void(size_t, size_t)> handler)
		{
			informationProgressHandler_ = std::move(handler);
		}

		void set_file_drag_handler(std::function<void(std::vector<std::filesystem::path>)> handler)
		{
			fileDragHandler_ = std::move(handler);
		}

		void set_fit(bool fit);
		bool fit() const { return primaryView_.zoom.is_fit(); }
		void zoom(double factor);
		bool has_displayed_image() const { return displayed_image() != nullptr; }
		std::vector<std::filesystem::path> selected_paths() const;
		size_t selection_count() const { return active_selection().size(); }
		int focused_index() const { return active_selection().focus(); }
		void select_all();
		void clear_selection();
		bool cancel_transient_state();
		void select_index(int index, bool extend, bool toggle);
		void step_focus(int direction);
		platform::WindowFramePtr frame() const { return frame_; }
		const std::filesystem::path& path() const { return path_; }
		int splitter_position() const { return splitterPosition_; }
		void set_splitter_position(int position);

	private:
		struct ThumbnailRequest
		{
			std::filesystem::path path;
			std::uint64_t generation{};
			size_t index{};
			int width{};
			int height{};
		};

		struct ThumbnailResult : ThumbnailRequest
		{
			std::vector<std::uint32_t> pixels;
			int originalWidth{};
			int originalHeight{};
		};

		struct Thumbnail
		{
			int width{};
			int height{};
			std::vector<std::uint32_t> pixels;
			int originalWidth{};
			int originalHeight{};
		};

		struct ImageRequest
		{
			std::filesystem::path path;
			std::uint64_t generation{};
			int width{};
			int height{};
			bool fullSize{};
			bool comparison{};
		};

		struct ImageResult : ImageRequest
		{
			files::DecodedImage image;
			files::DecodedImage half;
			files::DecodedImage quarter;
		};

		// Bounces between the UI thread and the metadata queue, one chunk of paths per trip.
		struct MetadataJob
		{
			std::uint64_t generation{};
			std::vector<std::filesystem::path> paths;
			bool aggregate{};
			size_t selectionCount{};
			size_t offset{};
			std::vector<files::Metadata> values;
		};

		struct Property
		{
			std::wstring name;
			std::wstring value;
			int nameWidth{};
			int valueWidth{};
		};

		struct RetainedImage
		{
			std::filesystem::path path;
			files::DecodedImage initial;
			files::DecodedImage full;
			files::DecodedImage half;
			files::DecodedImage quarter;
		};

		// One presented image: the pane it occupies plus its independent fit, zoom, and pan state.
		struct ImageView
		{
			app::recti bounds;
			ui::ZoomModel zoom;
			int sourceWidth{};
			int sourceHeight{};
		};

		enum class ScrollDrag { none, image, items, thumbnails };

		enum class InputEvent
		{
			leftButtonDown,
			leftButtonDoubleClick,
			rightButtonDown,
			rightButtonUp,
			leftButtonUp,
			move,
			wheel,
			leave,
			contextMenu,
			keyDown,
			captureLost
		};

		enum class ThumbnailState : std::uint8_t { none, queued, resident, skipped, evicted };

		// One tile of the multi-selection collage. Each cell decodes at its own size rather than
		// borrowing the thumbnail cache, which is capped far below the size a collage cell reaches.
		struct CollageCell
		{
			std::filesystem::path path;
			int index{-1};
			files::ItemKind kind{files::ItemKind::other};
			app::sizei source;
			app::recti bounds;
			files::DecodedImage image;
			ThumbnailState state{ThumbnailState::none};
		};

		platform::MessageResult message(const platform::WindowFramePtr& frame,
		                                platform::WindowMessage message) override;
		platform::MessageResult files_dropped(const platform::WindowFramePtr& frame,
		                                      const platform::FileDrop& drop) override;
		platform::MessageResult mouse(const platform::WindowFramePtr& frame, platform::MouseMessage message,
		                              const platform::MouseInput& input) override;
		platform::MessageResult key(const platform::WindowFramePtr& frame, platform::KeyMessage message,
		                            const platform::KeyInput& input) override;
		platform::MessageResult gesture(const platform::WindowFramePtr& frame,
		                                const platform::GestureInput& input) override;
		void paint(const platform::WindowFramePtr& frame, platform::DrawContext& draw) override;
		void size(const platform::WindowFramePtr& frame, app::sizei extent,
		          platform::MeasureContext& measure) override;
		platform::MessageResult handle_input(InputEvent event, const platform::MouseInput& mouse = {},
		                                     const platform::KeyInput& key = {});
		void paint_items(const ui::CanvasRenderer& renderer);
		void paint_properties(const ui::CanvasRenderer& renderer);
		void paint_property_panel(const ui::CanvasRenderer& renderer, const std::wstring& title,
		                          const std::vector<Property>& rows, app::recti panel, bool expanded,
		                          std::uint8_t opacity) const;
		void paint_details_header(const ui::CanvasRenderer& renderer);
		void paint_details(const ui::CanvasRenderer& renderer);
		void paint_tiles(const ui::CanvasRenderer& renderer);
		void paint_collage(const ui::CanvasRenderer& renderer) const;
		bool collage_active() const;
		void rebuild_collage();
		void arrange_collage();
		void queue_collage();
		void apply_collage(std::uint64_t generation, size_t cell, files::DecodedImage image);
		void rebuild_layout();
		void invalidate() const { if (frame_) frame_->invalidate(); }
		void mark_layout_dirty() { layoutDirty_ = true; }
		void ensure_layout() { if (layoutDirty_) rebuild_layout(); }
		int metric(const int logicalValue) const { return ui::scale_metric(logicalValue, dpi_); }
		std::array<app::recti, 5> details_columns(app::recti row) const;
		void rebuild_properties();
		void measure_properties();
		void measure_property_panels();
		app::recti measure_property_panel(const std::wstring& title, const std::vector<Property>& rows,
		                             app::recti available, bool rightAligned, bool expanded) const;
		const files::Metadata* cached_metadata(const std::filesystem::path& path) const;
		std::vector<Property> summary_properties(int index, std::vector<std::filesystem::path>& pending) const;
		void queue_metadata(std::vector<std::filesystem::path> paths, bool aggregate, size_t selectionCount);
		void pump_metadata(MetadataJob job);
		void apply_metadata(const MetadataJob& job);
		void trim_metadata_cache();
		int hit_test_item(app::pointi point) const;
		app::recti item_rect(int index) const;
		void invalidate_item(int index) const;
		void invalidate_image_pane() const;
		int splitter_x() const;
		void ensure_visible(int index);
		int spatial_neighbor(int index, int horizontal, int vertical) const;
		void notify_selection();
		void queue_thumbnails();
		void reset_thumbnail_cache();
		void request_image(ImageRequest request);
		void pump_image();
		void apply_image(ImageResult result);
		void retain_displayed_image();
		bool restore_retained_image(const std::filesystem::path& path, std::uint64_t generation);
		void clear_displayed_image();
		void trim_retained_images();
		void apply_thumbnail(ThumbnailResult result);
		static ThumbnailResult decode_thumbnail(const ThumbnailRequest& request);
		bool image_hit(app::pointi point) const;
		bool comparison_hit(app::pointi point) const;
		bool inspect_hit(app::pointi point) const;
		bool immersive_zoom() const;
		double fit_scale(bool comparison = false) const;
		double effective_scale(bool comparison) const;
		app::recti image_destination(double scale, bool comparison = false) const;
		void set_zoom(double scale, app::pointi focus);
		void step_zoom(int direction, app::pointi focus);
		void zoom_at(double factor, app::pointi focus);
		void reset_zoom(double scale);
		void pan_by(int x, int y);
		void begin_quick_zoom(app::pointi point);
		void begin_inspect_candidate(app::pointi point);
		void commit_quick_zoom();
		void update_quick_zoom(app::pointi point);
		void cancel_quick_zoom();
		void end_quick_zoom();
		void request_full_image();
		void request_comparison_image(int index);
		void select_fullscreen_item(int index, bool secondary);
		void step_fullscreen_selection(int direction, bool secondary);
		void step_displayed(int direction);
		void activate_index(int index);
		const files::DecodedImage* displayed_image() const;
		ui::SelectionModel& active_selection() { return fullscreen_ ? fullscreenSelection_ : selection_; }
		const ui::SelectionModel& active_selection() const { return fullscreen_ ? fullscreenSelection_ : selection_; }
		ImageView& active_view() { return activeComparison_ ? comparisonView_ : primaryView_; }
		const ImageView& active_view() const { return activeComparison_ ? comparisonView_ : primaryView_; }
		ImageView& view(const bool comparison) { return comparison ? comparisonView_ : primaryView_; }
		const ImageView& view(const bool comparison) const { return comparison ? comparisonView_ : primaryView_; }

		platform::WindowFramePtr frame_;
		std::shared_ptr<FrameReactor> reactorBinding_;
		std::filesystem::path path_;
		std::array<files::DecodedImage, 6> typeIcons_;
		std::array<files::DecodedImage, 6> detailTypeIcons_;
		std::array<bool, 6> typeIconAlpha_{};
		std::vector<files::FolderItem> items_;
		files::DecodedImage initialImage_;
		files::DecodedImage fullImage_;
		files::DecodedImage halfImage_;
		files::DecodedImage quarterImage_;
		files::DecodedImage comparisonImage_;
		std::filesystem::path comparisonPath_;
		std::filesystem::path normalDisplayedPath_;
		std::vector<std::filesystem::path> files_;
		std::vector<Thumbnail> thumbnails_;
		std::vector<ThumbnailState> thumbnailStates_;
		std::deque<size_t> thumbnailResidency_;
		std::vector<CollageCell> collage_;
		std::deque<RetainedImage> retainedImages_;
		ui::Element rootElement_;
		ui::Element itemFlow_;
		ui::Element propertyFlow_;
		std::vector<app::recti> itemBounds_;
		app::recti imagePane_;
		app::recti propertyPanelBounds_;
		app::recti comparisonPanelBounds_;
		app::recti propertyToggleBounds_;
		app::recti comparisonPropertyToggleBounds_;
		app::recti propertyHeaderBounds_;
		app::recti comparisonPropertyHeaderBounds_;
		app::recti stripHeaderBounds_;
		app::recti stripToggleBounds_;
		app::recti splitterBounds_;
		app::recti itemsViewport_;
		app::recti detailsHeaderBounds_;
		int detailsColumnsX_{};
		int detailsColumnsWidth_{};
		ui::VerticalScroll imageScroll_;
		ui::VerticalScroll itemsScroll_;
		ui::HorizontalScroll thumbnailScroll_;
		ui::SelectionModel selection_;
		ui::SelectionModel fullscreenSelection_;
		std::vector<Property> properties_;
		std::vector<Property> comparisonProperties_;
		std::map<std::filesystem::path, files::Metadata> metadataCache_;
		std::deque<std::filesystem::path> metadataOrder_;
		ImageView primaryView_;
		ImageView comparisonView_;
		ui::LoadingModel primaryLoading_;
		ui::LoadingModel comparisonLoading_;
		int thumbnailSize_{128};
		unsigned int dpi_{ui::default_dpi};
		size_t totalItemCount_{};
		size_t thumbnailTarget_{};
		bool fullSizePending_{};
		bool decodePending_{};
		bool decodeFailed_{};
		files::ItemKind displayedKind_{files::ItemKind::other};
		int hoverIndex_{-1};
		int pressedIndex_{-1};
		int contentHeight_{};
		int stripContentWidth_{};
		int stripOffset_{};
		int imageContentHeight_{};
		int splitterPosition_{-1};
		int scrollDragOffset_{};
		int wheelAccumulator_{};
		ScrollDrag scrollDrag_{ScrollDrag::none};
		bool layoutDirty_{true};
		bool hasFocus_{};
		bool draggingSplitter_{};
		bool hoveringSplitter_{};
		bool draggingMarquee_{};
		bool deferSingleSelection_{};
		bool draggingFiles_{};
		size_t marqueeCount_{};
		app::pointi marqueeStart_{};
		app::pointi marqueeCurrent_{};
		app::pointi fileDragStart_{};
		bool fullscreen_{};
		ui::ZoomModel normalZoom_;
		bool activeComparison_{};
		bool propertiesExpanded_{true};
		bool comparisonPropertiesExpanded_{true};
		bool thumbnailsExpanded_{true};
		bool quickZoom_{};
		double quickZoomScale_{1.0};
		double quickZoomX_{0.5};
		double quickZoomY_{0.5};
		bool panningImage_{};
		bool selectingZoom_{};
		bool suppressContextMenu_{};
		app::pointi imageDragOrigin_{};
		app::pointi imageDragPoint_{};
		app::pointi zoomSelectionStart_{};
		app::pointi zoomSelectionCurrent_{};
		std::uint64_t gestureZoomDistance_{};
		double gestureZoomStart_{};
		app::pointi gesturePanPoint_{};
		ItemMode mode_{ItemMode::thumbnails};
		files::SortField sortField_{files::SortField::name};
		bool sortAscending_{true};
		platform::FontPtr textFont_;

		// Background work is dispatched one job at a time per queue; these track what is outstanding.
		std::optional<ImageRequest> pendingImage_;
		std::optional<ImageRequest> pendingComparison_;
		bool imageInFlight_{};
		bool thumbnailsInFlight_{};
		bool collageInFlight_{};
		std::uint64_t generation_{};
		std::uint64_t imageGeneration_{};
		std::uint64_t comparisonGeneration_{};
		std::uint64_t metadataGeneration_{};
		std::uint64_t collageGeneration_{};
		int collageDecodeSize_{};
		size_t thumbnailsComplete_{};
		std::function<void(const std::filesystem::path&)> activationHandler_;
		std::function<void()> selectionHandler_;
		std::function<void(bool)> zoomModeHandler_;
		CommandHandlers commandHandlers_;
		std::function<void(app::pointi)> contextMenuHandler_;
		std::function<void(const std::vector<std::filesystem::path>&)> dropHandler_;
		std::function<void()> clearFilterHandler_;
		std::function<void(files::SortField, bool)> sortHandler_;
		std::function<void(size_t, size_t)> progressHandler_;
		std::function<void(const std::filesystem::path&)> loadErrorHandler_;
		std::function<void(size_t, size_t)> informationProgressHandler_;
		std::function<void(std::vector<std::filesystem::path>)> fileDragHandler_;

		// Expires with the canvas, so a completion its workers queued can tell that it has gone: a
		// headless pump may run one after the canvas that asked for it.
		std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
	};
}
