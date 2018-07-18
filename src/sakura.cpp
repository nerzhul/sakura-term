#include <gtk/gtk.h>
#include <vte/vte.h>
#include <glib/gstdio.h>
#include <iostream>
#include <cassert>
#include <libintl.h>
#include <gtkmm/window.h>
#include <gtkmm/notebook.h>
#include "sakura.h"
#include "debug.h"
#include "palettes.h"
#include "notebook.h"
#include "sakuraold.h"
#include "terminal.h"
#include "window.h"

static void sakura_on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto *obj = (Sakura *)data;
	obj->on_key_press(widget, event);
}



static void sakura_destroy_window(GtkWidget *widget, void *data)
{
	auto *obj = (Sakura *)data;
	obj->destroy(widget);
}

/* This function is used to fix bug #1393939 */
void sanitize_working_directory()
{
	const gchar *home_directory = g_getenv("HOME");
	if (home_directory == NULL) {
		home_directory = g_get_home_dir();
	}

	if (home_directory) {
		if (chdir(home_directory)) {
			fprintf(stderr, _("Cannot change working directory\n"));
			exit(1);
		}
	}
}

#define NOTEBOOK_CSS                                                                               \
	"* {\n"                                                                                    \
	"color : rgba(0,0,0,1.0);\n"                                                               \
	"background-color : rgba(0,0,0,1.0);\n"                                                    \
	"border-color : rgba(0,0,0,1.0);\n"                                                        \
	"}"

#define HTTP_REGEXP "(ftp|http)s?://[^ \t\n\b()<>{}«»\\[\\]\'\"]+[^.]"
#define MAIL_REGEXP "[^ \t\n\b]+@([^ \t\n\b]+\\.)+([a-zA-Z]{2,4})"

Sakura::Sakura() :
	cfg(g_key_file_new()),
	provider(Gtk::CssProvider::create())
{
	// This object is a singleton
	assert(sakura == nullptr);
	sakura = this;

	if (!config.read()) {
		exit(EXIT_FAILURE);
	}

	config.monitor();

	main_window = new SakuraWindow(Gtk::WINDOW_TOPLEVEL, &config);

	/* set default title pattern from config or NULL */
	Terminal::tab_default_title = g_key_file_get_string(cfg, cfg_group,
		"tab_default_title", NULL);

	term_data_id = g_quark_from_static_string("sakura_term");

	/* Use always GTK header bar*/
	g_object_set(gtk_settings_get_default(), "gtk-dialogs-use-header", TRUE, NULL);

	gchar *css = g_strdup_printf(NOTEBOOK_CSS);
	provider->load_from_data(std::string(css));
	auto context = main_window->notebook->get_style_context();
	context->add_provider(provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);

	/* Command line optionsNULL initialization */

	/* Set argv for forked childs. Real argv vector starts at argv[1] because we're
	   using G_SPAWN_FILE_AND_ARGV_ZERO to be able to launch login shells */
	argv[0] = g_strdup(g_getenv("SHELL"));
	if (option_login) {
		argv[1] = g_strdup_printf("-%s", g_getenv("SHELL"));
	} else {
		argv[1] = g_strdup(g_getenv("SHELL"));
	}
	argv[2] = NULL;

	if (option_title) {
		main_window->set_title(option_title);
	}

	if (option_columns) {
		columns = option_columns;
	}

	if (option_rows) {
		rows = option_rows;
	}

	if (option_font) {
		config.font = pango_font_description_from_string(option_font);
	}

	if (option_colorset && option_colorset > 0 && option_colorset <= NUM_COLORSETS) {
		config.last_colorset = option_colorset;
	}

	/* These options are exclusive */
	if (option_fullscreen) {
		sakura_fullscreen(nullptr, this);
	} else if (option_maximize) {
		main_window->maximize();
	}

	GError *error = nullptr;
	http_vteregexp = vte_regex_new_for_match(HTTP_REGEXP, strlen(HTTP_REGEXP), 0, &error);
	if (!http_vteregexp) {
		SAY("http_regexp: %s", error->message);
		g_error_free(error);
	}

	error = nullptr;
	mail_vteregexp = vte_regex_new_for_match(MAIL_REGEXP, strlen(MAIL_REGEXP), 0, &error);
	if (!mail_vteregexp) {
		SAY("mail_regexp: %s", error->message);
		g_error_free(error);
	}

	init_popup();

	g_signal_connect(G_OBJECT(main_window->gobj()), "destroy",
		G_CALLBACK(sakura_destroy_window), this);
	g_signal_connect(G_OBJECT(main_window->gobj()), "key-press-event",
		G_CALLBACK(sakura_on_key_press), this);
	g_signal_connect(G_OBJECT(main_window->gobj()), "show",
		G_CALLBACK(sakura_window_show_event), NULL);
	// g_signal_connect(G_OBJECT(notebook), "focus-in-event",
	// G_CALLBACK(sakura_notebook_focus_in), NULL);

	/* Add initial tabs (1 by default) */
	for (int i = 0; i < option_ntabs; i++)
		sakura_add_tab();

	sanitize_working_directory();
}

Sakura::~Sakura()
{
	for (uint8_t i=0; i < 3; i++) {
		if (argv[i]) {
			free(argv[i]);
		}
	}

	if (http_vteregexp) {
		vte_regex_unref(http_vteregexp);
	}

	if (mail_vteregexp) {
		vte_regex_unref(mail_vteregexp);
	}
}

static const gint BACKWARDS = 2;

static guint sakura_tokeycode(guint key)
{
	GdkKeymap *keymap;
	GdkKeymapKey *keys;
	gint n_keys;
	guint res = 0;

	keymap = gdk_keymap_get_for_display(gdk_display_get_default());

	if (gdk_keymap_get_entries_for_keyval(keymap, key, &keys, &n_keys)) {
		if (n_keys > 0) {
			res = keys[0].keycode;
		}
		g_free(keys);
	}

	return res;
}

void Sakura::destroy(GtkWidget *)
{
	SAY("Destroying sakura");

	g_key_file_free(cfg);

	pango_font_description_free(config.font);

	gtk_main_quit();
}

void Sakura::init_popup()
{
	GtkWidget *item_new_tab, *item_set_name, *item_close_tab, *item_copy, *item_paste,
		*item_select_font, *item_select_colors, *item_set_title, *item_fullscreen,
		*item_toggle_scrollbar, *item_options, *item_show_first_tab,
		*item_urgent_bell, *item_audible_bell, *item_blinking_cursor,
		*item_allow_bold, *item_other_options, *item_cursor, *item_cursor_block,
		*item_cursor_underline, *item_cursor_ibeam, *item_palette,
		*item_palette_tango, *item_palette_linux, *item_palette_xterm,
		*item_palette_rxvt, *item_palette_solarized_dark,
		*item_palette_solarized_light, *item_palette_gruvbox,
		*item_show_close_button, *item_tabs_on_bottom, *item_less_questions,
		*item_disable_numbered_tabswitch, *item_use_fading,
		*item_stop_tab_cycling_at_end_tabs;
	GtkWidget *options_menu, *other_options_menu, *cursor_menu, *palette_menu;

	item_open_mail = gtk_menu_item_new_with_label(_("Open mail"));
	item_open_link = gtk_menu_item_new_with_label(_("Open link"));
	item_copy_link = gtk_menu_item_new_with_label(_("Copy link"));
	item_new_tab = gtk_menu_item_new_with_label(_("New tab"));
	item_set_name = gtk_menu_item_new_with_label(_("Set tab name..."));
	item_close_tab = gtk_menu_item_new_with_label(_("Close tab"));
	item_fullscreen = gtk_menu_item_new_with_label(_("Full screen"));
	item_copy = gtk_menu_item_new_with_label(_("Copy"));
	item_paste = gtk_menu_item_new_with_label(_("Paste"));
	item_select_font = gtk_menu_item_new_with_label(_("Select font..."));
	item_select_colors = gtk_menu_item_new_with_label(_("Select colors..."));
	item_set_title = gtk_menu_item_new_with_label(_("Set window title..."));

	item_options = gtk_menu_item_new_with_label(_("Options"));

	item_other_options = gtk_menu_item_new_with_label(_("More"));
	item_show_first_tab = gtk_check_menu_item_new_with_label(_("Always show tab bar"));
	item_tabs_on_bottom = gtk_check_menu_item_new_with_label(_("Tabs at bottom"));
	item_show_close_button = gtk_check_menu_item_new_with_label(_("Show close button on tabs"));
	item_toggle_scrollbar = gtk_check_menu_item_new_with_label(_("Show scrollbar"));
	item_less_questions = gtk_check_menu_item_new_with_label(_("Don't show exit dialog"));
	item_urgent_bell = gtk_check_menu_item_new_with_label(_("Set urgent bell"));
	item_audible_bell = gtk_check_menu_item_new_with_label(_("Set audible bell"));
	item_blinking_cursor = gtk_check_menu_item_new_with_label(_("Set blinking cursor"));
	item_allow_bold = gtk_check_menu_item_new_with_label(_("Enable bold font"));
	item_stop_tab_cycling_at_end_tabs =
		gtk_check_menu_item_new_with_label(_("Stop tab cycling at end tabs"));
	item_disable_numbered_tabswitch =
		gtk_check_menu_item_new_with_label(_("Disable numbered tabswitch"));
	item_use_fading = gtk_check_menu_item_new_with_label(_("Enable focus fade"));
	item_cursor = gtk_menu_item_new_with_label(_("Set cursor type"));
	item_cursor_block = gtk_radio_menu_item_new_with_label(NULL, _("Block"));
	item_cursor_underline = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_cursor_block), _("Underline"));
	item_cursor_ibeam = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_cursor_block), _("IBeam"));
	item_palette = gtk_menu_item_new_with_label(_("Set palette"));
	item_palette_tango = gtk_radio_menu_item_new_with_label(NULL, "Tango");
	item_palette_linux = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_palette_tango), "Linux");
	item_palette_gruvbox = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_palette_tango), "Gruvbox");
	item_palette_xterm = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_palette_tango), "Xterm");
	item_palette_rxvt = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_palette_tango), "rxvt");
	item_palette_solarized_dark = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_palette_tango), "Solarized dark");
	item_palette_solarized_light = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_palette_tango), "Solarized light");

	if (config.first_tab) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_show_first_tab), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_show_first_tab), FALSE);
	}

	if (config.show_closebutton) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_show_close_button), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_show_close_button), FALSE);
	}

	if (config.tabs_on_bottom) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_tabs_on_bottom), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_tabs_on_bottom), FALSE);
	}

	if (config.less_questions) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_less_questions), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_less_questions), FALSE);
	}

	if (config.show_scrollbar) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_toggle_scrollbar), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_toggle_scrollbar), FALSE);
	}

	if (config.disable_numbered_tabswitch) {
		gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(item_disable_numbered_tabswitch), TRUE);
	} else {
		gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(item_disable_numbered_tabswitch), FALSE);
	}

	if (config.use_fading) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_use_fading), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_use_fading), FALSE);
	}

	if (config.urgent_bell) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_urgent_bell), TRUE);
	}

	if (config.audible_bell) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_audible_bell), TRUE);
	}

	if (config.blinking_cursor) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_blinking_cursor), TRUE);
	}

	if (config.allow_bold) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_allow_bold), TRUE);
	}

	if (config.stop_tab_cycling_at_end_tabs) {
		gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(item_stop_tab_cycling_at_end_tabs), TRUE);
	}

	switch (config.cursor_type) {
		case VTE_CURSOR_SHAPE_BLOCK:
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_block), TRUE);
			break;
		case VTE_CURSOR_SHAPE_UNDERLINE:
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_underline), TRUE);
			break;
		case VTE_CURSOR_SHAPE_IBEAM:
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_ibeam), TRUE);
	}

	if (config.palette_str == "linux") {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_palette_linux), TRUE);
	} else if (config.palette_str == "gruvbox") {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_palette_gruvbox), TRUE);
	} else if (config.palette_str == "xterm") {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_palette_tango), TRUE);
	} else if (config.palette_str == "rxvt") {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_palette_rxvt), TRUE);
	} else if (config.palette_str == "tango") {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_palette_xterm), TRUE);
	} else if (config.palette_str == "solarized_dark") {
		gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(item_palette_solarized_dark), TRUE);
	} else {
		gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(item_palette_solarized_light), TRUE);
	}

	open_link_separator = gtk_separator_menu_item_new();

	menu = gtk_menu_new();
	// sakura->labels_menu=gtk_menu_new();

	/* Add items to popup menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_open_mail);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_open_link);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_copy_link);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), open_link_separator);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_new_tab);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_set_name);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_close_tab);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_fullscreen);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_copy);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_paste);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_options);

	options_menu = gtk_menu_new();
	other_options_menu = gtk_menu_new();
	cursor_menu = gtk_menu_new();
	palette_menu = gtk_menu_new();

	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_set_title);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_select_colors);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_select_font);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_other_options);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_show_first_tab);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_tabs_on_bottom);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_show_close_button);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_toggle_scrollbar);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_less_questions);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_urgent_bell);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_audible_bell);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_disable_numbered_tabswitch);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_use_fading);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_blinking_cursor);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_allow_bold);
	gtk_menu_shell_append(
		GTK_MENU_SHELL(other_options_menu), item_stop_tab_cycling_at_end_tabs);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_cursor);
	gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_block);
	gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_underline);
	gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_ibeam);
	gtk_menu_shell_append(GTK_MENU_SHELL(other_options_menu), item_palette);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_tango);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_linux);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_gruvbox);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_xterm);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_rxvt);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_solarized_dark);
	gtk_menu_shell_append(GTK_MENU_SHELL(palette_menu), item_palette_solarized_light);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_options), options_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_other_options), other_options_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_cursor), cursor_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_palette), palette_menu);

	/* ... and finally assign callbacks to menuitems */
	g_signal_connect(G_OBJECT(item_new_tab), "activate", G_CALLBACK(sakura_new_tab), NULL);
	g_signal_connect(G_OBJECT(item_set_name), "activate", G_CALLBACK(sakura_set_name_dialog),
		NULL);
	g_signal_connect(G_OBJECT(item_close_tab), "activate", G_CALLBACK(sakura_close_tab), NULL);
	g_signal_connect(G_OBJECT(item_select_font), "activate", G_CALLBACK(sakura_font_dialog),
		NULL);
	g_signal_connect(G_OBJECT(item_copy), "activate", G_CALLBACK(sakura_copy), NULL);
	g_signal_connect(G_OBJECT(item_paste), "activate", G_CALLBACK(sakura_paste), NULL);
	g_signal_connect(G_OBJECT(item_select_colors), "activate", G_CALLBACK(sakura_color_dialog),
		NULL);

	g_signal_connect(G_OBJECT(item_show_first_tab), "activate",
		G_CALLBACK(sakura_show_first_tab), NULL);
	g_signal_connect(G_OBJECT(item_tabs_on_bottom), "activate",
		G_CALLBACK(sakura_tabs_on_bottom), NULL);
	g_signal_connect(G_OBJECT(item_less_questions), "activate",
		G_CALLBACK(sakura_less_questions), NULL);
	g_signal_connect(G_OBJECT(item_show_close_button), "activate",
		G_CALLBACK(sakura_show_close_button), NULL);
	g_signal_connect(G_OBJECT(item_toggle_scrollbar), "activate",
		G_CALLBACK(sakura_show_scrollbar), NULL);
	g_signal_connect(G_OBJECT(item_urgent_bell), "activate", G_CALLBACK(sakura_urgent_bell),
		NULL);
	g_signal_connect(G_OBJECT(item_audible_bell), "activate", G_CALLBACK(sakura_audible_bell),
		NULL);
	g_signal_connect(G_OBJECT(item_blinking_cursor), "activate",
		G_CALLBACK(sakura_blinking_cursor), NULL);
	g_signal_connect(
		G_OBJECT(item_allow_bold), "activate", G_CALLBACK(sakura_allow_bold), NULL);
	g_signal_connect(G_OBJECT(item_stop_tab_cycling_at_end_tabs), "activate",
		G_CALLBACK(sakura_stop_tab_cycling_at_end_tabs), NULL);
	g_signal_connect(G_OBJECT(item_disable_numbered_tabswitch), "activate",
		G_CALLBACK(sakura_disable_numbered_tabswitch), sakura);
	g_signal_connect(
		G_OBJECT(item_use_fading), "activate", G_CALLBACK(sakura_use_fading), NULL);
	g_signal_connect(G_OBJECT(item_set_title), "activate", G_CALLBACK(sakura_set_title_dialog),
		NULL);
	g_signal_connect(G_OBJECT(item_cursor_block), "activate", G_CALLBACK(sakura_set_cursor),
		(gpointer) "block");
	g_signal_connect(G_OBJECT(item_cursor_underline), "activate", G_CALLBACK(sakura_set_cursor),
		(gpointer) "underline");
	g_signal_connect(G_OBJECT(item_cursor_ibeam), "activate", G_CALLBACK(sakura_set_cursor),
		(gpointer) "ibeam");
	g_signal_connect(G_OBJECT(item_palette_tango), "activate", G_CALLBACK(sakura_set_palette),
		(gpointer) "tango");
	g_signal_connect(G_OBJECT(item_palette_linux), "activate", G_CALLBACK(sakura_set_palette),
		(gpointer) "linux");
	g_signal_connect(G_OBJECT(item_palette_gruvbox), "activate", G_CALLBACK(sakura_set_palette),
		(gpointer) "gruvbox");
	g_signal_connect(G_OBJECT(item_palette_xterm), "activate", G_CALLBACK(sakura_set_palette),
		(gpointer) "xterm");
	g_signal_connect(G_OBJECT(item_palette_rxvt), "activate", G_CALLBACK(sakura_set_palette),
		(gpointer) "rxvt");
	g_signal_connect(G_OBJECT(item_palette_solarized_dark), "activate",
		G_CALLBACK(sakura_set_palette), (gpointer) "solarized_dark");
	g_signal_connect(G_OBJECT(item_palette_solarized_light), "activate",
		G_CALLBACK(sakura_set_palette), (gpointer) "solarized_light");

	g_signal_connect(G_OBJECT(item_open_mail), "activate", G_CALLBACK(sakura_open_mail),
		NULL);
	g_signal_connect(G_OBJECT(item_open_link), "activate", G_CALLBACK(sakura_open_url),
		NULL);
	g_signal_connect(G_OBJECT(item_copy_link), "activate", G_CALLBACK(sakura_copy_url),
		NULL);
	g_signal_connect(G_OBJECT(item_fullscreen), "activate", G_CALLBACK(sakura_fullscreen),
		this);

	gtk_widget_show_all(menu);
}

static gboolean
terminal_screen_image_draw_cb (GtkWidget *widget, cairo_t *cr, void *userdata)
{
	auto *obj = (Terminal *)userdata;
	GdkRectangle target_rect;
	GtkAllocation alloc;
	cairo_surface_t *child_surface;
	cairo_t *child_cr;

	if (!obj->bg_image)
		return FALSE;

	gtk_widget_get_allocation (widget, &alloc);

	target_rect.x = 0;
	target_rect.y = 0;
	target_rect.width = alloc.width;
	target_rect.height = alloc.height;

	child_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, alloc.width, alloc.height);
	child_cr = cairo_create (child_surface);

	g_signal_handler_block (widget, obj->bg_image_callback_id);
	gtk_widget_draw (widget, child_cr);
	g_signal_handler_unblock (widget, obj->bg_image_callback_id);

	gdk_cairo_set_source_pixbuf (cr, obj->bg_image, 0, 0);
	cairo_pattern_set_extend (cairo_get_source (cr), CAIRO_EXTEND_REPEAT);

	gdk_cairo_rectangle (cr, &target_rect);
	cairo_fill (cr);

	cairo_set_source_surface (cr, child_surface, 0, 0);
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_paint (cr);

	cairo_destroy (child_cr);
	cairo_surface_destroy (child_surface);

	return TRUE;
}

/* Set the terminal colors for all notebook tabs */
void Sakura::set_colors()
{
	int i;
	int n_pages = main_window->notebook->get_n_pages();
	Terminal *term;

	for (i = (n_pages - 1); i >= 0; i--) {
		term = sakura_get_page_term(this, i);

		if (!config.get_background_image().empty()) {
			if (!term->bg_image_callback_id) {
				term->bg_image_callback_id = g_signal_connect (term->hbox, "draw",
					G_CALLBACK(terminal_screen_image_draw_cb), term);
			}

			g_clear_object (&term->bg_image);
			GError *error = nullptr;
			term->bg_image = gdk_pixbuf_new_from_file(config.get_background_image().c_str(),
				&error);
			if (error) {
				SAY("Failed to load background image %s", error->message);
				g_clear_error(&error);
			}

			gtk_widget_queue_draw(GTK_WIDGET(term->hbox));
		}

		backcolors[term->colorset].alpha = config.get_background_alpha();

		vte_terminal_set_colors(VTE_TERMINAL(term->vte),
			&forecolors[term->colorset], &backcolors[term->colorset], config.palette,
			PALETTE_SIZE);
		vte_terminal_set_color_cursor(
			VTE_TERMINAL(term->vte), &curscolors[term->colorset]);


	}

	/* Main window opacity must be set. Otherwise vte widget will remain opaque */
	sakura->main_window->set_opacity(backcolors[term->colorset].alpha);
}

gboolean Sakura::on_key_press(GtkWidget *widget, GdkEventKey *event)
{
	if (event->type != GDK_KEY_PRESS)
		return FALSE;

	uint32_t topage = 0;

	gint npages = main_window->notebook->get_n_pages();

	/* Use keycodes instead of keyvals. With keyvals, key bindings work only in
	 * US/ISO8859-1 and similar locales */
	guint keycode = event->hardware_keycode;

	/* Add/delete tab keybinding pressed */
	if ((event->state & config.add_tab_accelerator) == config.add_tab_accelerator &&
			keycode == sakura_tokeycode(config.keymap.add_tab_key)) {
		sakura_add_tab();
		return TRUE;
	} else if ((event->state & sakura->config.del_tab_accelerator) ==
					config.del_tab_accelerator &&
			keycode == sakura_tokeycode(config.keymap.del_tab_key)) {
		/* Delete current tab */
		close_tab(nullptr);
		return TRUE;
	}

	/* Switch tab keybinding pressed (numbers or next/prev) */
	/* In cases when the user configured accelerators like these ones:
		switch_tab_accelerator=4  for ctrl+next[prev]_tab_key
		move_tab_accelerator=5  for ctrl+shift+next[prev]_tab_key
	   move never works, because switch will be processed first, so it needs to be
	   fixed with the following condition */
	if (((event->state & config.switch_tab_accelerator) ==
			    sakura->config.switch_tab_accelerator) &&
			((event->state & config.move_tab_accelerator) !=
					config.move_tab_accelerator)) {

		if ((keycode >= sakura_tokeycode(GDK_KEY_1)) &&
				(keycode <= sakura_tokeycode(GDK_KEY_9))) {

			/* User has explicitly disabled this branch, make sure to
			 * propagate the event */
			if (config.disable_numbered_tabswitch)
				return FALSE;

			if (sakura_tokeycode(GDK_KEY_1) == keycode)
				topage = 0;
			else if (sakura_tokeycode(GDK_KEY_2) == keycode)
				topage = 1;
			else if (sakura_tokeycode(GDK_KEY_3) == keycode)
				topage = 2;
			else if (sakura_tokeycode(GDK_KEY_4) == keycode)
				topage = 3;
			else if (sakura_tokeycode(GDK_KEY_5) == keycode)
				topage = 4;
			else if (sakura_tokeycode(GDK_KEY_6) == keycode)
				topage = 5;
			else if (sakura_tokeycode(GDK_KEY_7) == keycode)
				topage = 6;
			else if (sakura_tokeycode(GDK_KEY_8) == keycode)
				topage = 7;
			else if (sakura_tokeycode(GDK_KEY_9) == keycode)
				topage = 8;
			if (topage <= npages)
				main_window->notebook->set_current_page(topage);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.prev_tab_key)) {
			if (main_window->notebook->get_current_page() == 0) {
				main_window->notebook->set_current_page(npages - 1);
			} else {
				gtk_notebook_prev_page(main_window->notebook->gobj());
			}
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.next_tab_key)) {
			if (main_window->notebook->get_current_page() == (npages - 1)) {
				main_window->notebook->set_current_page(0);
			} else {
				main_window->notebook->next_page();
			}
			return TRUE;
		}
	}

	/* Move tab keybinding pressed */
	if (((event->state & config.move_tab_accelerator) == config.move_tab_accelerator)) {
		if (keycode == sakura_tokeycode(config.keymap.prev_tab_key)) {
			sakura_move_tab(BACKWARDS);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.next_tab_key)) {
			sakura_move_tab(FORWARD);
			return TRUE;
		}
	}

	/* Copy/paste keybinding pressed */
	if ((event->state & config.copy_accelerator) == config.copy_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.copy_key)) {
			sakura_copy(NULL, NULL);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.paste_key)) {
			sakura_paste(NULL, NULL);
			return TRUE;
		}
	}

	/* Show scrollbar keybinding pressed */
	if ((event->state & config.scrollbar_accelerator) == config.scrollbar_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.scrollbar_key)) {
			sakura_show_scrollbar(NULL, NULL);
			return TRUE;
		}
	}

	/* Set tab name keybinding pressed */
	if ((event->state & config.set_tab_name_accelerator) == config.set_tab_name_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.set_tab_name_key)) {
			sakura_set_name_dialog(NULL, NULL);
			return TRUE;
		}
	}

	/* Search keybinding pressed */
	if ((event->state & config.search_accelerator) == config.search_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.search_key)) {
			sakura_search_dialog(NULL, NULL);
			return TRUE;
		}
	}

	/* Increase/decrease font size keybinding pressed */
	if ((event->state & config.font_size_accelerator) == config.font_size_accelerator) {
		if (keycode == sakura_tokeycode(config.keymap.increase_font_size_key)) {
			sakura_increase_font(NULL, NULL);
			return TRUE;
		} else if (keycode == sakura_tokeycode(config.keymap.decrease_font_size_key)) {
			sakura_decrease_font(NULL, NULL);
			return TRUE;
		}
	}

	/* F11 (fullscreen) pressed */
	if (keycode == sakura_tokeycode(config.keymap.fullscreen_key)) {
		toggle_fullscreen(nullptr);
		return TRUE;
	}

	/* Change in colorset */
	if ((event->state & config.set_colorset_accelerator) == config.set_colorset_accelerator) {
		int i;
		for (i = 0; i < NUM_COLORSETS; i++) {
			if (keycode == sakura_tokeycode(config.keymap.set_colorset_keys[i])) {
				sakura_set_colorset(i);
				return TRUE;
			}
		}
	}
	return FALSE;
}

void Sakura::on_child_exited(GtkWidget *widget)
{
	gint page = gtk_notebook_page_num(main_window->notebook->gobj(), gtk_widget_get_parent(widget));
	gint npages = main_window->notebook->get_n_pages();
	auto *term = sakura_get_page_term(this, page);

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	if (option_hold == TRUE) {
		SAY("hold option has been activated");
		return;
	}

	/* Child should be automatically reaped because we don't use G_SPAWN_DO_NOT_REAP_CHILD flag
	 */
	g_spawn_close_pid(term->pid);

	del_tab(page, true);
}

void Sakura::on_eof(GtkWidget *widget)
{
	SAY("Got EOF signal");

	gint npages = main_window->notebook->get_n_pages();

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	/* Workaround for libvte strange behaviour. There is not child-exited signal for
	   the last terminal, so we need to kill it here.  Check with libvte authors about
	   child-exited/eof signals */
	if (main_window->notebook->get_current_page() == 0) {

		auto *term = sakura_get_page_term(this, 0);

		if (option_hold == TRUE) {
			SAY("hold option has been activated");
			return;
		}

		// SAY("waiting for terminal pid (in eof) %d", term->pid);
		// waitpid(term->pid, &status, WNOHANG);
		/* TODO: check wait return */
		/* Child should be automatically reaped because we don't use
		 * G_SPAWN_DO_NOT_REAP_CHILD flag */
		g_spawn_close_pid(term->pid);

		del_tab(0, true);
	}
}

void Sakura::on_page_removed(GtkWidget *widget)
{
	if (main_window->notebook->get_n_pages() == 1) {
		/* If the first tab is disabled, window size changes and we need
		 * to recalculate its size */
		sakura_set_size();
	}
}

void Sakura::close_tab(GtkWidget *)
{
	pid_t pgid;
	GtkWidget *dialog;
	gint response;
	Terminal *term;

	gint page = main_window->notebook->get_current_page();
	gint npages = main_window->notebook->get_n_pages();
	term = sakura_get_page_term(this, page);

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	/* Check if there are running processes for this tab. Use tcgetpgrp to compare to the shell
	 * PGID */
	pgid = tcgetpgrp(vte_pty_get_fd(vte_terminal_get_pty(VTE_TERMINAL(term->vte))));

	if ((pgid != -1) && (pgid != term->pid) && (!config.less_questions)) {
		dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL,
				GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("There is a running process in this terminal.\n\nDo you really "
				  "want to close it?"));

		response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		if (response == GTK_RESPONSE_YES) {
			del_tab(page, true);
		}
	} else {
		del_tab(page, true);
	}
}
/* Delete the notebook tab passed as a parameter */
void Sakura::del_tab(gint page, bool exit_if_needed)
{
	auto *term = sakura_get_page_term(this, page);
	gint npages = main_window->notebook->get_n_pages();

	/* When there's only one tab use the shell title, if provided */
	if (npages == 2) {
		term = sakura_get_page_term(this, 0);
		const char *title = vte_terminal_get_window_title(VTE_TERMINAL(term->vte));
		if (title)
			gtk_window_set_title(GTK_WINDOW(main_window), title);
	}

	term = sakura_get_page_term(this, page);

	/* Do the first tab checks BEFORE deleting the tab, to ensure correct
	 * sizes are calculated when the tab is deleted */
	if (npages == 2) {
		if (config.first_tab) {
			main_window->notebook->set_show_tabs(true);
		} else {
			main_window->notebook->set_show_tabs(false);
		}
		sakura->keep_fc = true;
	}

	gtk_widget_hide(term->hbox);
	main_window->notebook->remove_page(page);

	/* Find the next page, if it exists, and grab focus */
	if (main_window->notebook->get_n_pages() > 0) {
		page = main_window->notebook->get_current_page();
		term = sakura_get_page_term(this, page);
		gtk_widget_grab_focus(term->vte);
	}

	if (exit_if_needed) {
		if (main_window->notebook->get_n_pages() == 0)
			destroy(nullptr);
	}
}

void Sakura::toggle_fullscreen(GtkWidget *)
{
	if (!m_fullscreen) {
		gtk_window_fullscreen(GTK_WINDOW(main_window));
	} else {
		gtk_window_unfullscreen(GTK_WINDOW(main_window));
	}

	m_fullscreen = !m_fullscreen;
}

void Sakura::toggle_numbered_tabswitch_option(GtkWidget *widget)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		config.disable_numbered_tabswitch = true;
		sakura_set_config_boolean("disable_numbered_tabswitch", TRUE);
	} else {
		config.disable_numbered_tabswitch = false;
		sakura_set_config_boolean("disable_numbered_tabswitch", FALSE);
	}
}

void Sakura::beep(GtkWidget *widget)
{
	// Remove the urgency hint. This is necessary to signal the window manager
	// that a new urgent event happened when the urgent hint is set after this.
	main_window->set_urgency_hint(false);

	if (config.urgent_bell) {
		main_window->set_urgency_hint(true);
	}
}
