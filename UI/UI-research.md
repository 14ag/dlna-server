C++ GTK4 GUI Implementation Guide: Rendering Penpot MCP Designs with B00merang Windows 10 AssetsArchitectural Overview and Design PipelineTranslating digital interface specifications into high-performance desktop software requires bridging vector design canvases and native GUI layout engines. In modern Linux application development with C++ and GTK4, achieving visual and geometric parity with target design prototypes—such as a Windows 10 design system modeled in Penpot—requires a deterministic extraction and rendering pipeline. Relying on manual measurement or static vector exports introduces visual drift, misaligned flex containers, and broken window manager integration.The architectural pipeline outlined in this guide establishes a deterministic data flow between Penpot design documents and native GTK4 application structures. The pipeline uses the Model Context Protocol (MCP) to query live structural trees, flex layout parameters, and spatial bounds directly from Penpot's internal plugin environment. These spatial vectors are combined with window frame chrome, iconography, and visual styling assets sourced from the B00merang-Project/windows-10 theme engine.The ingestion pipeline begins within Penpot, where layout structures are evaluated programmatically via MCP function calls such as penpotUtils.shapeStructure(). The returned layout metadata provides exact pixel measurements, flex directions, paddings, and element gaps.Simultaneously, visual styling assets—including SVG graphic primitives, icon sets, and light-theme system palettes—are loaded from the local B00merang-Project/windows-10 asset tree.These visual and structural inputs converge within the C++ application layer, where spatial geometries are tokenized into explicit integer constants while surface aesthetics are bound using GTK CSS providers (GtkCssProvider).To prevent host desktop themes (such as GNOME Adwaita) from overriding the custom styling, the rendering engine neutralizes default toolkit themes and enforces Client-Side Decorations (CSD) via gtk_window_set_titlebar().Phase 1: Penpot Layout Extraction and MCP AutomationIngesting Structural Hierarchies via Model Context ProtocolThe Model Context Protocol (MCP) bridges language-model-driven development tools and Penpot design documents by exposing real-time queries against Penpot's internal object tree. Rather than processing static raster or flat vector extracts, developers execute JavaScript within the Penpot plugin runtime using mcp__penpot__execute_code. This enables structural queries via the penpotUtils utility library.To extract bounding boxes, flexbox containers, paddings, gaps, and font metrics across the workspace, programmatic traversal begins at the root design board:JavaScript// Programmatic extraction of Penpot design hierarchy and flex parameters
function extractDesignTokens(rootNode, maxDepth = 4) {
    const structure = penpotUtils.shapeStructure(rootNode, maxDepth);
    const flexContainers = penpotUtils.findShapes(
        shape => shape.type === 'board' || shape.type === 'group',
        rootNode
    );
    
    return {
        tree: structure,
        containers: flexContainers.map(container => ({
            id: container.id,
            name: container.name,
            bounds: {
                x: container.x,
                y: container.y,
                width: container.width,
                height: container.height
            },
            flex: container.flex ? {
                dir: container.flex.dir,
                rowGap: container.flex.rowGap,
                columnGap: container.flex.columnGap,
                padding: {
                    top: container.flex.topPadding,
                    right: container.flex.rightPadding,
                    bottom: container.flex.bottomPadding,
                    left: container.flex.leftPadding
                },
                alignItems: container.flex.alignItems,
                justifyContent: container.flex.justifyContent
            } : null
        }))
    };
}

return extractDesignTokens(penpot.root, 5);
Mapping Penpot Layout Constructs to GTK4 Container WidgetsLayout constructs exported via Penpot MCP map directly to equivalent GTK4 container widgets and CSS spatial properties. Penpot relies primarily on CSS Flexbox semantics for dynamic containers, which translate to GTK4 GtkBox, GtkCenterBox, and GtkGrid containers.Penpot Flex / Shape AttributeGTK4 Architecture ConstructEquivalent GTK CSS / API BindingFlex Direction: dir = "row"GtkBox with GTK_ORIENTATION_HORIZONTALgtk_box_new(GTK_ORIENTATION_HORIZONTAL, gap)Flex Direction: dir = "column"GtkBox with GTK_ORIENTATION_VERTICALgtk_box_new(GTK_ORIENTATION_VERTICAL, gap)Flex Gap (rowGap / columnGap)GtkBox spacing parametergtk_box_set_spacing(box, gap_px)Container Padding (topPadding, etc.)GTK CSS Padding / Marginsmargin-top, margin-left, padding: 8pxItem Alignment (alignItems = "center")Widget Alignment Propertiesgtk_widget_set_valign(widget, GTK_ALIGN_CENTER)Horizontal Expand (horizontalSizing = "fill")Hexpand Widget Propertiesgtk_widget_set_hexpand(widget, TRUE)Absolute Coordinate ShapesGtkFixed Coordinate Placementgtk_fixed_put(fixed, widget, x_px, y_px)Handling Flex Order Anomalies and Child Placement RulesWhen processing Penpot flex layouts programmatically, child node ordering in the DOM array may run inverse to visual layout order. In Penpot flex containers where direction is configured as column or row, historical engine rules reverse the internal array relative to visual stack order unless naturalChildOrdering is explicitly enabled.When mapping layout trees to GTK4 structures, developers must first validate whether penpot.flags.naturalChildOrdering is active within the Penpot document context. If naturalChildOrdering is false, the array iteration sequence must be inverted before appending child elements to a GtkBox or GtkGrid to prevent upside-down menu stacks or flipped toolbar buttons. Explicit layout sizes extracted from Penpot must be bound to GTK widgets using gtk_widget_set_size_request(widget, width, height) to enforce visual boundaries.Phase 2: Theme Asset Integration and GTK4 CSS Engine CustomizationB00merang Windows 10 Asset Resource MappingThe visual design system relies on static assets—such as SVG/PNG graphic primitives, window control icons, and color rules—from the B00merang-Project/windows-10 repository. Standard desktop deployment requires locating these assets across multiple runtime installation paths, including system share directories, local application bundles, or executable paths.Path discovery uses a fallback strategy to resolve runtime image assets reliably across Linux environments:C++// Executable-relative resource path resolution algorithm
std::string ResolveBundledResourcePath(const std::string& fileName) {
    if (fileName.empty()) return {};
    const std::string exeDir = ExecutableDirectory();
    const std::vector<std::string> candidates = {
        std::string(DLNA_RESOURCE_DIR) + "/" + fileName,
        exeDir + "/" + fileName,
        exeDir + "/resources/" + fileName,
        exeDir + "/../resources/" + fileName,
        exeDir + "/../share/dlna-server/" + fileName,
        exeDir + "/../share/dlna-server/icons/" + fileName,
        exeDir + "/../Resources/" + fileName,
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
            return candidate;
        }
    }
    return {};
}
Neutralizing Default Toolkit Themes (Adwaita Overrides)Modern GTK4 applications automatically load GNOME's libadwaita or default Adwaita CSS rules. These default rules impose background gradients, rounded button corners, and standard headerbar layouts that conflict with Windows 10 visual styling.To grant custom CSS rules total control over window frame rendering, set the environment variable GTK_THEME to Default prior to initializing GtkApplication. This strips decorative engine defaults and resets the toolkit baseline.C++// Disable external theme engines before initializing GTK
g_setenv("GTK_THEME", "Default", FALSE);
GtkApplication* app = gtk_application_new("com.github.dlna-server-14ag", G_APPLICATION_DEFAULT_FLAGS);
Multi-Tiered CSS Provider Injection and Class HierarchyVisual styling is applied via GTK CSS providers added to the default GdkDisplay. To maintain clean separation between base theme assets and design-specific visual overlays, stylesheets are registered using priority offsets:C++void OnAppStartup(GtkApplication* app, gpointer) {
    GdkDisplay* display = gdk_display_get_default();
    
    // Tier 1: Base Windows 10 GTK Theme Rules
    const std::string cssPath = ResolveBundledResourcePath("gtk/style.css");
    if (!cssPath.empty()) {
        GtkCssProvider* provider = gtk_css_provider_new();
        gtk_css_provider_load_from_path(provider, cssPath.c_str());
        gtk_style_context_add_provider_for_display(
            display, 
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_USER
        );
        g_object_unref(provider);
    }

    // Tier 2: Specific Visual Overlay Rules (overrides Tier 1 at higher priority)
    const std::string figmaCssPath = ResolveBundledResourcePath("gtk/figma.css");
    if (!figmaCssPath.empty()) {
        GtkCssProvider* figmaProvider = gtk_css_provider_new();
        gtk_css_provider_load_from_path(figmaProvider, figmaCssPath.c_str());
        gtk_style_context_add_provider_for_display(
            display, 
            GTK_STYLE_PROVIDER(figmaProvider),
            GTK_STYLE_PROVIDER_PRIORITY_USER + 2
        );
        g_object_unref(figmaProvider);
    }
}
Graphic primitives from the B00merang-Project/windows-10 repository map to specific GTK CSS node rules:Asset CategoryTarget Path / PrimitiveGTK CSS Selector TargetCSS Rule ImplementationWindow BackgroundLight Gray #F0F0F0window.main-window, .dlna-main-surfacebackground-color: #f0f0f0; border: 1px solid #707070;Titlebar (Active)Accent #0078D7 or #FFFFFFheaderbar.win10-titlebar, .win10-activebackground-color: #ffffff; color: #000000;Titlebar (Inactive)Neutral #E6E6E6.win10-inactivebackground-color: #e6e6e6; color: #7f7f7f;Close Button Iconassets/close.svgbutton.win10-close-btnbackground-image: url('assets/close.svg'); background-size: contain;Close Hover StateCrimson #E81123button.win10-close-btn:hoverbackground-color: #e81123; background-image: url('assets/close-white.svg');Dialog ButtonsWarm Gray Borderbutton.dlna-dialog-buttonborder: 1px solid #acacac; background-color: #e1e1e1;Default ActionAccent Highlightbutton.suggested-actionbackground-color: #0078d7; color: #ffffff; border: 1px solid #005499;Phase 3: Client-Side Decoration (CSD) and Custom Titlebar EngineWindow Chrome Replacement ArchitectureStandard GTK window management delegates titlebars and window control buttons to the underlying window manager or default CSD implementation. To recreate the flat 1px-bordered window chrome of Windows 10, native window manager decorations must be overridden and replaced with a custom CSD titlebar widget structure.C++// Disable native decorations and attach a custom client-side titlebar
GtkWidget* window = gtk_application_window_new(app);
gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
gtk_window_set_titlebar(
    GTK_WINDOW(window),
    CreateWin10Titlebar(GTK_WINDOW(window), "DLNA Server", WindowChrome::Main)
);
Titlebar Window Handle and State-Aware CSS StylingA custom titlebar must support window drag operations via GtkWindowHandle and update visually when window focus changes. The visual titlebar container consists of a GtkWindowHandle wrapping a main horizontal GtkBox (win10-titlebar-box). Inside, a left-aligned sub-box houses the 16x16 pixel application icon and window title string label, followed by a expanding spacer box (hexpand = TRUE), and packed window control buttons. Binding to the window's notify::is-active GObject property ensures the titlebar toggles between active (.win10-active) and inactive (.win10-inactive) CSS state classes dynamically.C++struct TitlebarState {
    GtkWindow* window;
    GtkWidget* titlebar;
};

static void UpdateTitlebarActiveState(TitlebarState* state) {
    const bool active = gtk_window_is_active(state->window);
    if (active) {
        gtk_widget_add_css_class(state->titlebar, "win10-active");
        gtk_widget_remove_css_class(state->titlebar, "win10-inactive");
    } else {
        gtk_widget_add_css_class(state->titlebar, "win10-inactive");
        gtk_widget_remove_css_class(state->titlebar, "win10-active");
    }
}

static void OnTitlebarActiveNotify(GObject*, GParamSpec*, gpointer userData) {
    UpdateTitlebarActiveState(static_cast<TitlebarState*>(userData));
}

GtkWidget* CreateWin10Titlebar(GtkWindow* window, const char* title, WindowChrome chrome) {
    gtk_window_set_title(window, title);

    GtkWidget* handle = gtk_window_handle_new();
    gtk_widget_add_css_class(handle, "win10-titlebar");
    gtk_widget_set_size_request(handle, -1, UiTokensPosix::kTitlebarHeight);

    GtkWidget* titlebar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(titlebar, "win10-titlebar-box");
    gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), titlebar);

    GtkWidget* leftBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(leftBox, UiTokensPosix::kTitlebarLeftPadding);
    gtk_widget_set_valign(leftBox, GTK_ALIGN_CENTER);

    if (chrome == WindowChrome::Main) {
        GtkWidget* icon = gtk_image_new_from_icon_name("dlna-server");
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
        gtk_box_append(GTK_BOX(leftBox), icon);
    }

    GtkWidget* titleLabel = gtk_label_new(title);
    gtk_widget_add_css_class(titleLabel, "title");
    gtk_label_set_xalign(GTK_LABEL(titleLabel), 0.0f);
    gtk_box_append(GTK_BOX(leftBox), titleLabel);
    gtk_box_append(GTK_BOX(titlebar), leftBox);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(titlebar), spacer);

    GtkWidget* controls = gtk_window_controls_new(GTK_PACK_END);
    gtk_window_controls_set_decoration_layout(
        GTK_WINDOW_CONTROLS(controls),
        chrome == WindowChrome::Main ? ":minimize,close" : ":close"
    );
    gtk_box_append(GTK_BOX(titlebar), controls);

    TitlebarState* state = g_new0(TitlebarState, 1);
    state->window = window;
    state->titlebar = handle;
    g_object_set_data_full(G_OBJECT(handle), "win10-titlebar-state", state, g_free);

    UpdateTitlebarActiveState(state);
    g_signal_connect(window, "notify::is-active", G_CALLBACK(OnTitlebarActiveNotify), state);

    return handle;
}
Custom Window Controls and Signal BindingWhen replacing native window manager controls with custom GtkButton elements, standard GTK button styling must be cleared using custom CSS node rules. Click signals are then bound directly to the target window instance:CSS/* Custom Close Button Styling */
button.custom-win10-close {
    background-image: url('assets/close.svg');
    background-size: contain;
    background-repeat: no-repeat;
    background-position: center;
    border: none;
    box-shadow: none;
    min-width: 46px;
    min-height: 30px;
    padding: 0;
    background-color: transparent;
}

button.custom-win10-close:hover {
    background-color: #e81123;
    background-image: url('assets/close-hover.svg');
}
C++// Wire close button signal directly to window close action
GtkWidget* closeButton = gtk_button_new();
gtk_widget_add_css_class(closeButton, "custom-win10-close");
g_signal_connect_swapped(closeButton, "clicked", G_CALLBACK(gtk_window_close), window);
Phase 4: Tokenization and C++ GTK4 Component ConstructionGeometric Tokenization (UiTokensPosix Framework)To preserve spatial parity across the application, layout dimensions extracted from Penpot MCP are defined as integer constants in a dedicated header file. This structure isolates POSIX/GTK layout constants from shared platform logic, preventing unexpected regressions on other target platforms.C++#ifndef UI_TOKENS_POSIX_H
#define UI_TOKENS_POSIX_H

namespace UiTokensPosix {

constexpr int kTitlebarHeight = 30;
constexpr int kTitlebarLeftPadding = 10;

constexpr int kMainWindowWidth = 426;
constexpr int kMainWindowHeight = 593;
constexpr int kMainToolbarHeight = 57;
constexpr int kMainSourceListX = 21;
constexpr int kMainSourceListYFromListArea = 52;
constexpr int kMainSourceListWidth = 385;
constexpr int kMainSourceListHeight = 430;

constexpr int kAddButtonX = 103, kAddButtonY = 14, kAddButtonW = 54, kAddButtonH = 30;
constexpr int kDeleteButtonX = 167, kDeleteButtonY = 14, kDeleteButtonW = 70, kDeleteButtonH = 30;
constexpr int kStartStopButtonX = 248, kStartStopButtonY = 14, kStartStopButtonW = 70, kStartStopButtonH = 30;
constexpr int kSettingsButtonX = 327, kSettingsButtonY = 14, kSettingsButtonW = 81, kSettingsButtonH = 30;

constexpr int kSettingsWindowWidth = 700;
constexpr int kSettingsWindowHeight = 797;
constexpr int kSettingsRibbonHeight = 23;
constexpr int kSettingsRibbonLogsX = 10;
constexpr int kSettingsRibbonHelpX = 54;

constexpr int kServerGroupX = 20, kServerGroupY = 28, kServerGroupW = 660, kServerGroupH = 213;
constexpr int kGeneralGroupX = 24, kGeneralGroupY = 281, kGeneralGroupW = 308, kGeneralGroupH = 124;
constexpr int kPlaylistGroupX = 348, kPlaylistGroupY = 281, kPlaylistGroupW = 330, kPlaylistGroupH = 124;
constexpr int kMediaGroupX = 24, kMediaGroupY = 445, kMediaGroupW = 660, kMediaGroupH = 216;

constexpr int kLogWindowWidth = 772;
constexpr int kLogWindowHeight = 712;

constexpr int kHelpWindowWidth = 530;
constexpr int kHelpWindowHeight = 400;

constexpr int kSourcePromptWindowWidth = 538;
constexpr int kSourcePromptWindowHeight = 209;

constexpr int kWarningWindowWidth = 245;
constexpr int kWarningWindowHeight = 150;
constexpr int kWarningMessageAreaH = 76;
constexpr int kWarningFooterH = 44;
constexpr int kWarningOkW = 73, kWarningOkH = 22;

}  // namespace UiTokensPosix

#endif  // UI_TOKENS_POSIX_H
Main Interface Window Layout ImplementationThe main window implementation uses a fixed layout container (GtkFixed) to position child widgets—such as toolbars, action buttons, status bands, and scrollable lists—at exact pixel coordinates derived from the design spec.C++void BuildMainWindow(GtkApplication* app) {
    GtkWidget* window = gtk_application_window_new(app);
    g_mainWindow = window;
    InstallMnemonicCueControllers(window);
    
    gtk_window_set_title(GTK_WINDOW(window), "DLNA Server");
    gtk_window_set_default_size(GTK_WINDOW(window), UiTokensPosix::kMainWindowWidth, UiTokensPosix::kMainWindowHeight);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_titlebar(GTK_WINDOW(window), CreateWin10Titlebar(GTK_WINDOW(window), "DLNA Server", WindowChrome::Main));

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, UiTokensPosix::kMainWindowWidth, UiTokensPosix::kMainWindowHeight);
    gtk_widget_add_css_class(fixed, "dlna-main-surface");
    gtk_window_set_child(GTK_WINDOW(window), fixed);

    // Toolbar Header Box
    g_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(g_toolbar, UiTokensPosix::kMainWindowWidth, UiTokensPosix::kMainToolbarHeight);
    gtk_fixed_put(GTK_FIXED(fixed), g_toolbar, 0, 0);
    gtk_widget_add_css_class(g_toolbar, "toolbar");

    // Action Buttons placed at fixed coordinates
    g_addButton = gtk_button_new_with_label("Add");
    gtk_widget_set_size_request(g_addButton, UiTokensPosix::kAddButtonW, UiTokensPosix::kAddButtonH);
    gtk_fixed_put(GTK_FIXED(fixed), g_addButton, UiTokensPosix::kAddButtonX, UiTokensPosix::kAddButtonY);
    gtk_widget_add_css_class(g_addButton, "toolbar-button");
    
    g_removeButton = gtk_button_new_with_label("Delete");
    gtk_widget_set_size_request(g_removeButton, UiTokensPosix::kDeleteButtonW, UiTokensPosix::kDeleteButtonH);
    gtk_fixed_put(GTK_FIXED(fixed), g_removeButton, UiTokensPosix::kDeleteButtonX, UiTokensPosix::kDeleteButtonY);
    gtk_widget_add_css_class(g_removeButton, "toolbar-button");

    g_startStopButton = gtk_button_new_with_label("Start");
    gtk_widget_set_size_request(g_startStopButton, UiTokensPosix::kStartStopButtonW, UiTokensPosix::kStartStopButtonH);
    gtk_fixed_put(GTK_FIXED(fixed), g_startStopButton, UiTokensPosix::kStartStopButtonX, UiTokensPosix::kStartStopButtonY);
    gtk_widget_add_css_class(g_startStopButton, "toolbar-button");

    g_settingsButton = gtk_button_new_with_label("Settings");
    gtk_widget_set_size_request(g_settingsButton, UiTokensPosix::kSettingsButtonW, UiTokensPosix::kSettingsButtonH);
    gtk_fixed_put(GTK_FIXED(fixed), g_settingsButton, UiTokensPosix::kSettingsButtonX, UiTokensPosix::kSettingsButtonY);

    // Source List Scroll Area
    const int listTop = UiTokensPosix::kMainToolbarHeight + UiTokensPosix::kMainSourceListYFromListArea;
    g_sourcesScrolled = gtk_scrolled_window_new();
    gtk_widget_add_css_class(g_sourcesScrolled, "source-list");
    gtk_widget_set_size_request(g_sourcesScrolled, UiTokensPosix::kMainSourceListWidth, UiTokensPosix::kMainSourceListHeight);
    gtk_fixed_put(GTK_FIXED(fixed), g_sourcesScrolled, UiTokensPosix::kMainSourceListX, listTop);

    g_sources = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_sources), GTK_SELECTION_SINGLE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(g_sourcesScrolled), g_sources);

    gtk_window_present(GTK_WINDOW(window));
}
Complex Sub-Dialog Implementations and Modality RulesSecondary dialog windows (such as Settings, Log views, or Warning prompts) instantiate dedicated layout structures styled to match the main window frame.C++bool ShowSettingsDialog() {
    if (g_settingsDialog != nullptr) return false;

    g_settingsDialog = gtk_window_new();
    InstallMnemonicCueControllers(g_settingsDialog);
    gtk_window_set_title(GTK_WINDOW(g_settingsDialog), "DLNA Server Settings");
    gtk_window_set_default_size(GTK_WINDOW(g_settingsDialog), UiTokensPosix::kSettingsWindowWidth, UiTokensPosix::kSettingsWindowHeight);
    gtk_window_set_resizable(GTK_WINDOW(g_settingsDialog), FALSE);

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, UiTokensPosix::kSettingsWindowWidth, UiTokensPosix::kSettingsWindowHeight);
    gtk_window_set_child(GTK_WINDOW(g_settingsDialog), fixed);
    gtk_widget_add_css_class(fixed, "dlna-dialog-body");

    // Helper lambdas for creating group box frames and entries
    auto makeFrame = [&](const char* label, int x, int y, int w, int h) {
        GtkWidget* frame = gtk_frame_new(label);
        gtk_widget_add_css_class(frame, "dlna-groupbox");
        gtk_widget_set_size_request(frame, w, h);
        gtk_fixed_put(GTK_FIXED(fixed), frame, x, y);
        gtk_frame_set_label_align(GTK_FRAME(frame), 0.0f);
        return frame;
    };

    makeFrame("Server", UiTokensPosix::kServerGroupX, UiTokensPosix::kServerGroupY, UiTokensPosix::kServerGroupW, UiTokensPosix::kServerGroupH);
    makeFrame("General", UiTokensPosix::kGeneralGroupX, UiTokensPosix::kGeneralGroupY, UiTokensPosix::kGeneralGroupW, UiTokensPosix::kGeneralGroupH);
    makeFrame("Playlist", UiTokensPosix::kPlaylistGroupX, UiTokensPosix::kPlaylistGroupY, UiTokensPosix::kPlaylistGroupW, UiTokensPosix::kPlaylistGroupH);
    makeFrame("Media browsing", UiTokensPosix::kMediaGroupX, UiTokensPosix::kMediaGroupY, UiTokensPosix::kMediaGroupW, UiTokensPosix::kMediaGroupH);

    gtk_window_set_titlebar(
        GTK_WINDOW(g_settingsDialog),
        CreateWin10Titlebar(GTK_WINDOW(g_settingsDialog), "DLNA Server Settings", WindowChrome::Dialog)
    );

    PresentModalChild(GTK_WINDOW(g_settingsDialog), GTK_WINDOW(g_mainWindow));
    while (gtk_widget_get_visible(g_settingsDialog)) {
        g_main_context_iteration(nullptr, TRUE);
    }
    ClearActiveModal(GTK_WINDOW(g_settingsDialog));
    return g_settingsSaved;
}
The table below outlines geometry specifications across all primary windows and sub-dialogs:Window / Dialog ModuleWidth (px)Height (px)Primary Child Layout EngineDominant CSS ClassesMain Window Surface426593GtkFixed Coordinate Canvas.dlna-main-surface, .toolbarSettings Dialog700797Grouped GtkFrame Grids.dlna-dialog-body, .dlna-groupboxApplication Log Viewer772712Monospace GtkTextView.dlna-log-surfaceHelp / Document Window530400Tabbed PangoLayout Stream.dlna-help-surfaceAdd Media Source Prompt538209Absolute Form Input Rows.dlna-dialog-entry-warm-borderPlaylist Entry Prompt538189Two-Column Field Form.suggested-actionWarning / Alert Box245150Vertical Message & Footer Box.dlna-warning-message, .dlna-warning-footerPhase 5: Event-Driven Lifecycle, Focus Gating, and Native System ParityModal Hierarchy and Focus Stealing PreventionModal sub-dialogs must prevent input events from reaching parent windows. Tracking the top-level active modal pointer (g_activeModal) ensures child windows retain focus and prevents background click-throughs:C++static GtkWindow* g_activeModal = nullptr;

void PresentModalChild(GtkWindow* child, GtkWindow* parent) {
    if (parent != nullptr) {
        gtk_window_set_transient_for(child, parent);
    }
    gtk_window_set_modal(child, TRUE);
    gtk_window_present(child);
    g_activeModal = child;
}

void ClearActiveModal(GtkWindow* child) {
    if (g_activeModal == child) {
        g_activeModal = nullptr;
    }
}
To prevent the main window from stealing focus when clicked while a modal child is open, attach a focus event controller to the main window that redirects input back to the active modal child:C++// Guard main window against focus stealing during active modal states
GtkEventController* mainFocus = gtk_event_controller_focus_new();
gtk_widget_add_controller(GTK_WIDGET(g_mainWindow), mainFocus);
g_signal_connect(mainFocus, "enter", G_CALLBACK(+[](GtkEventController*, gpointer) -> gboolean {
    if (g_activeModal != nullptr) {
        gtk_window_present(GTK_WINDOW(g_activeModal));
        return FALSE; // Deny focus to main window
    }
    return TRUE;
}), nullptr);
Mnemonic Cue State and Input RoutingWindows 10 applications show keyboard mnemonics (underlined access keys) when triggered via keyboard navigation, hiding them during mouse interactions. Installing capture-phase event controllers on top-level windows mirrors this behavior in GTK4:C++void InstallMnemonicCueControllers(GtkWidget* window) {
    // Reveal access key underlines on keyboard input
    GtkEventController* keyWatcher = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keyWatcher, GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(window, keyWatcher);
    g_signal_connect(keyWatcher, "key-pressed",
        G_CALLBACK(+[](GtkEventController*, guint, guint, GdkModifierType, gpointer data) -> gboolean {
            g_cueState.OnKeyboardInput();
            ApplyMnemonicsVisible(GTK_WINDOW(data));
            return FALSE;
        }), window);

    // Hide access key underlines on mouse clicks
    GtkGesture* clickWatcher = gtk_gesture_click_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(clickWatcher), GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(clickWatcher));
    g_signal_connect(clickWatcher, "pressed",
        G_CALLBACK(+[](GtkGestureClick*, gint, gdouble, gdouble, gpointer data) {
            g_cueState.OnMouseButtonInput();
            ApplyMnemonicsVisible(GTK_WINDOW(data));
        }), window);
}
Safe Window Teardown and Graphics Server SyncDuring application shutdown or window destruction, queued X11/Wayland display protocol requests can execute against invalidated window surfaces, generating BadDrawable X11 errors. Safe window destruction requires flushing pending event loop iterations, synchronizing display queues, and installing a temporary X error handler:C++int g_ignore_baddrawable = 0;

static int g_x_error_handler(Display* display, XErrorEvent* error) {
    if (g_ignore_baddrawable && error->error_code == BadDrawable) {
        return 0; // Ignore BadDrawable during teardown
    }
    static int (*old_handler)(Display*, XErrorEvent*) = nullptr;
    if (!old_handler) {
        old_handler = XSetErrorHandler(nullptr);
        if (!old_handler) old_handler = XSetErrorHandler(g_x_error_handler);
    }
    return old_handler ? old_handler(display, error) : 0;
}

void DestroyMainWindowSafely() {
    if (g_mainWindow == nullptr) return;
    GtkWidget* toDestroy = g_mainWindow;
    g_mainWindow = nullptr;

    // Process all pending context iterations before destruction
    while (g_main_context_pending(nullptr)) {
        g_main_context_iteration(nullptr, FALSE);
    }

    GdkDisplay* display = gtk_widget_get_display(toDestroy);
    if (display != nullptr) {
        gdk_display_sync(display);
    }

    g_ignore_baddrawable = 1;
    XSetErrorHandler(g_x_error_handler);

    gtk_window_destroy(GTK_WINDOW(toDestroy));

    g_ignore_baddrawable = 0;
}
Conclusions and Implementation RecommendationsThe integration pipeline described in this guide provides a systematic method for bringing Penpot design specifications into native C++ GTK4 applications. Querying design geometry directly via the Model Context Protocol removes reliance on static visual extraction and eliminates layout drift. Combining these extracted parameters with assets from the B00merang-Project/windows-10 repository enables GTK applications to match target visual specifications precisely.Engineering teams implementing this pipeline should adhere to several key operational practices:Automate Geometry Extraction: Integrate programmatic tree queries via mcp__penpot__execute_code into the build workflow to generate geometric token headers automatically whenever design sources change.Isolate POSIX Layout Tokens: Store GTK-specific spatial dimensions in separate headers (such as ui_tokens_posix.h) to prevent GTK layout changes from affecting other platform builds.Suppress Default System Themes: Force GTK_THEME=Default at startup to prevent desktop themes (like GNOME Adwaita) from overriding custom CSS styling.Enforce Custom CSD Titlebars: Replace native window manager frames using gtk_window_set_titlebar() and track focus changes via notify::is-active to maintain visual consistency across desktop environments.Implement Robust Modal and Lifecycle Management: Track active modal windows to prevent background focus stealing, use capture-phase event controllers for key cues, and synchronize display queues with gdk_display_sync() during teardown to prevent protocol errors.