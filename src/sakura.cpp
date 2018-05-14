#include <gtk/gtk.h>
#include <vte/vte.h>
#include <glib/gstdio.h>
#include <iostream>
#include "sakura.h"
#include "debug.h"
#include "palettes.h"
#include "sakuraold.h"

#define NOTEBOOK_CSS "* {\n"\
	"color : rgba(0,0,0,1.0);\n"\
	"background-color : rgba(0,0,0,1.0);\n"\
	"border-color : rgba(0,0,0,1.0);\n"\
	"}"

/* make this an array instead of #defines to get a compile time
* error instead of a runtime if NUM_COLORSETS changes */
static int cs_keys[NUM_COLORSETS] =
		{GDK_KEY_F1, GDK_KEY_F2, GDK_KEY_F3, GDK_KEY_F4, GDK_KEY_F5, GDK_KEY_F6};

#define ICON_FILE "terminal-tango.svg"
#define DEFAULT_SCROLL_LINES 4096
#define HTTP_REGEXP "(ftp|http)s?://[^ \t\n\b()<>{}«»\\[\\]\'\"]+[^.]"
#define MAIL_REGEXP "[^ \t\n\b]+@([^ \t\n\b]+\\.)+([a-zA-Z]{2,4})"
#define DEFAULT_COLUMNS 80
#define DEFAULT_ROWS 24
#define DEFAULT_FONT "Ubuntu Mono,monospace 12"
#define DEFAULT_WORD_CHARS "-,./?%&#_~:"
#define DEFAULT_PALETTE "solarized_dark"

#define DEFAULT_ADD_TAB_ACCELERATOR  (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_DEL_TAB_ACCELERATOR  (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_SWITCH_TAB_ACCELERATOR  (GDK_CONTROL_MASK)
#define DEFAULT_MOVE_TAB_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_COPY_ACCELERATOR  (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_SCROLLBAR_ACCELERATOR  (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_OPEN_URL_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_FONT_SIZE_ACCELERATOR (GDK_CONTROL_MASK)
#define DEFAULT_SET_TAB_NAME_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_SEARCH_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_SELECT_COLORSET_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_ADD_TAB_KEY  GDK_KEY_T
#define DEFAULT_DEL_TAB_KEY  GDK_KEY_W
#define DEFAULT_PREV_TAB_KEY  GDK_KEY_Left
#define DEFAULT_NEXT_TAB_KEY  GDK_KEY_Right
#define DEFAULT_COPY_KEY  GDK_KEY_C
#define DEFAULT_PASTE_KEY  GDK_KEY_V
#define DEFAULT_SCROLLBAR_KEY  GDK_KEY_S
#define DEFAULT_SET_TAB_NAME_KEY  GDK_KEY_N
#define DEFAULT_SEARCH_KEY  GDK_KEY_F
#define DEFAULT_FULLSCREEN_KEY  GDK_KEY_F11
#define DEFAULT_INCREASE_FONT_SIZE_KEY GDK_KEY_plus
#define DEFAULT_DECREASE_FONT_SIZE_KEY GDK_KEY_minus

void Sakura::init()
{
	char* configdir = nullptr;
	int i;

	term_data_id = g_quark_from_static_string("sakura_term");

	/* Config file initialization*/
	sakura.cfg = g_key_file_new();
	sakura.config_modified=false;

	GError *error=NULL;
	/* Open config file */
	if (!g_key_file_load_from_file(sakura.cfg, sakura.configfile, (GKeyFileFlags) 0, &error)) {
		/* If there's no file, ignore the error. A new one is created */
		if (error->code==G_KEY_FILE_ERROR_UNKNOWN_ENCODING || error->code==G_KEY_FILE_ERROR_INVALID_VALUE) {
			g_error_free(error);
			fprintf(stderr, "Not valid config file format\n");
			exit(EXIT_FAILURE);
		}
	}

	/* Add GFile monitor to control file external changes */
	GFile *cfgfile = g_file_new_for_path(sakura.configfile);
	GFileMonitor *mon_cfgfile = g_file_monitor_file (cfgfile, (GFileMonitorFlags) 0, NULL, NULL);
	g_signal_connect(G_OBJECT(mon_cfgfile), "changed", G_CALLBACK(sakura_conf_changed), NULL);

	gchar *cfgtmp = NULL;

	/* We can safely ignore errors from g_key_file_get_value(), since if the
	 * call to g_key_file_has_key() was successful, the key IS there. From the
	 * glib docs I don't know if we can ignore errors from g_key_file_has_key,
	 * too. I think we can: the only possible error is that the config file
	 * doesn't exist, but we have just read it!
	 */

	for( i=0; i<NUM_COLORSETS; i++) {
		char temp_name[20];

		sprintf(temp_name, "colorset%d_fore", i+1);
		if (!g_key_file_has_key(sakura.cfg, cfg_group, temp_name, NULL)) {
			sakura_set_config_string(temp_name, "rgb(192,192,192)");
		}
		cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, temp_name, NULL);
		gdk_rgba_parse(&sakura.forecolors[i], cfgtmp);
		g_free(cfgtmp);

		sprintf(temp_name, "colorset%d_back", i+1);
		if (!g_key_file_has_key(sakura.cfg, cfg_group, temp_name, NULL)) {
			sakura_set_config_string(temp_name, "rgba(0,0,0,1)");
		}
		cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, temp_name, NULL);
		gdk_rgba_parse(&sakura.backcolors[i], cfgtmp);
		g_free(cfgtmp);

		sprintf(temp_name, "colorset%d_curs", i+1);
		if (!g_key_file_has_key(sakura.cfg, cfg_group, temp_name, NULL)) {
			sakura_set_config_string(temp_name, "rgb(255,255,255)");
		}
		cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, temp_name, NULL);
		gdk_rgba_parse(&sakura.curscolors[i], cfgtmp);
		g_free(cfgtmp);

		sprintf(temp_name, "colorset%d_key", i+1);
		if (!g_key_file_has_key(sakura.cfg, cfg_group, temp_name, NULL)) {
			sakura_set_keybind(temp_name, cs_keys[i]);
		}
		sakura.keymap.set_colorset_keys[i]= sakura_get_keybind(temp_name);
	}

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "last_colorset", NULL)) {
		sakura_set_config_integer("last_colorset", 1);
	}
	sakura.last_colorset = g_key_file_get_integer(sakura.cfg, cfg_group, "last_colorset", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "scroll_lines", NULL)) {
		g_key_file_set_integer(sakura.cfg, cfg_group, "scroll_lines", DEFAULT_SCROLL_LINES);
	}
	sakura.scroll_lines = g_key_file_get_integer(sakura.cfg, cfg_group, "scroll_lines", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "font", NULL)) {
		sakura_set_config_string("font", DEFAULT_FONT);
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "font", NULL);
	sakura.font = pango_font_description_from_string(cfgtmp);
	free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "show_always_first_tab", NULL)) {
		sakura_set_config_string("show_always_first_tab", "No");
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "show_always_first_tab", NULL);
	sakura.first_tab = (strcmp(cfgtmp, "Yes")==0) ? true : false;
	free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "scrollbar", NULL)) {
		sakura_set_config_boolean("scrollbar", FALSE);
	}
	sakura.show_scrollbar = g_key_file_get_boolean(sakura.cfg, cfg_group, "scrollbar", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "closebutton", NULL)) {
		sakura_set_config_boolean("closebutton", TRUE);
	}
	sakura.show_closebutton = g_key_file_get_boolean(sakura.cfg, cfg_group, "closebutton", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "tabs_on_bottom", NULL)) {
		sakura_set_config_boolean("tabs_on_bottom", FALSE);
	}
	sakura.tabs_on_bottom = g_key_file_get_boolean(sakura.cfg, cfg_group, "tabs_on_bottom", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "less_questions", NULL)) {
		sakura_set_config_boolean("less_questions", FALSE);
	}
	sakura.less_questions = g_key_file_get_boolean(sakura.cfg, cfg_group, "less_questions", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "disable_numbered_tabswitch", NULL)) {
		sakura_set_config_boolean("disable_numbered_tabswitch", FALSE);
	}
	sakura.disable_numbered_tabswitch = g_key_file_get_boolean(sakura.cfg, cfg_group, "disable_numbered_tabswitch", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "use_fading", NULL)) {
		sakura_set_config_boolean("use_fading", FALSE);
	}
	sakura.use_fading = g_key_file_get_boolean(sakura.cfg, cfg_group, "use_fading", NULL);

	if(!g_key_file_has_key(sakura.cfg, cfg_group, "scrollable_tabs", NULL)) {
		sakura_set_config_boolean("scrollable_tabs", TRUE);
	}
	sakura.scrollable_tabs = g_key_file_get_boolean(sakura.cfg, cfg_group, "scrollable_tabs", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "urgent_bell", NULL)) {
		sakura_set_config_string("urgent_bell", "Yes");
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "urgent_bell", NULL);
	sakura.urgent_bell= (strcmp(cfgtmp, "Yes")==0) ? 1 : 0;
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "audible_bell", NULL)) {
		sakura_set_config_string("audible_bell", "Yes");
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "audible_bell", NULL);
	sakura.audible_bell= (strcmp(cfgtmp, "Yes")==0) ? 1 : 0;
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "blinking_cursor", NULL)) {
		sakura_set_config_string("blinking_cursor", "No");
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "blinking_cursor", NULL);
	sakura.blinking_cursor= (strcmp(cfgtmp, "Yes")==0) ? 1 : 0;
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "stop_tab_cycling_at_end_tabs", NULL)) {
		sakura_set_config_string("stop_tab_cycling_at_end_tabs", "No");
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "stop_tab_cycling_at_end_tabs", NULL);
	sakura.stop_tab_cycling_at_end_tabs= (strcmp(cfgtmp, "Yes")==0) ? 1 : 0;
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "allow_bold", NULL)) {
		sakura_set_config_string("allow_bold", "Yes");
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "allow_bold", NULL);
	sakura.allow_bold= (strcmp(cfgtmp, "Yes")==0) ? 1 : 0;
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "cursor_type", NULL)) {
		sakura_set_config_string("cursor_type", "VTE_CURSOR_SHAPE_BLOCK");
	}
	sakura.cursor_type = (VteCursorShape) g_key_file_get_integer(sakura.cfg, cfg_group, "cursor_type", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "word_chars", NULL)) {
		sakura_set_config_string("word_chars", DEFAULT_WORD_CHARS);
	}
	sakura.word_chars = g_key_file_get_value(sakura.cfg, cfg_group, "word_chars", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "palette", NULL)) {
		sakura_set_config_string("palette", DEFAULT_PALETTE);
	}
	cfgtmp = g_key_file_get_string(sakura.cfg, cfg_group, "palette", NULL);
	if (strcmp(cfgtmp, "linux")==0) {
		sakura.palette=linux_palette;
	} else if (strcmp(cfgtmp, "gruvbox")==0) {
		sakura.palette=gruvbox_palette;
	} else if (strcmp(cfgtmp, "xterm")==0) {
		sakura.palette=xterm_palette;
	} else if (strcmp(cfgtmp, "rxvt")==0) {
		sakura.palette=rxvt_palette;
	} else if (strcmp(cfgtmp, "tango")==0) {
		sakura.palette=tango_palette;
	} else if (strcmp(cfgtmp, "solarized_dark")==0) {
		sakura.palette=solarized_dark_palette;
	} else {
		sakura.palette=solarized_light_palette;
	}
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "add_tab_accelerator", NULL)) {
		sakura_set_config_integer("add_tab_accelerator", DEFAULT_ADD_TAB_ACCELERATOR);
	}
	sakura.add_tab_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "add_tab_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "del_tab_accelerator", NULL)) {
		sakura_set_config_integer("del_tab_accelerator", DEFAULT_DEL_TAB_ACCELERATOR);
	}
	sakura.del_tab_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "del_tab_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "switch_tab_accelerator", NULL)) {
		sakura_set_config_integer("switch_tab_accelerator", DEFAULT_SWITCH_TAB_ACCELERATOR);
	}
	sakura.switch_tab_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "switch_tab_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "move_tab_accelerator", NULL)) {
		sakura_set_config_integer("move_tab_accelerator", DEFAULT_MOVE_TAB_ACCELERATOR);
	}
	sakura.move_tab_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "move_tab_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "copy_accelerator", NULL)) {
		sakura_set_config_integer("copy_accelerator", DEFAULT_COPY_ACCELERATOR);
	}
	sakura.copy_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "copy_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "scrollbar_accelerator", NULL)) {
		sakura_set_config_integer("scrollbar_accelerator", DEFAULT_SCROLLBAR_ACCELERATOR);
	}
	sakura.scrollbar_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "scrollbar_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "open_url_accelerator", NULL)) {
		sakura_set_config_integer("open_url_accelerator", DEFAULT_OPEN_URL_ACCELERATOR);
	}
	sakura.open_url_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "open_url_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "font_size_accelerator", NULL)) {
		sakura_set_config_integer("font_size_accelerator", DEFAULT_FONT_SIZE_ACCELERATOR);
	}
	sakura.font_size_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "font_size_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "set_tab_name_accelerator", NULL)) {
		sakura_set_config_integer("set_tab_name_accelerator", DEFAULT_SET_TAB_NAME_ACCELERATOR);
	}
	sakura.set_tab_name_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "set_tab_name_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "search_accelerator", NULL)) {
		sakura_set_config_integer("search_accelerator", DEFAULT_SEARCH_ACCELERATOR);
	}
	sakura.search_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "search_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "add_tab_key", NULL)) {
		sakura_set_keybind("add_tab_key", DEFAULT_ADD_TAB_KEY);
	}
	sakura.keymap.add_tab_key = sakura_get_keybind("add_tab_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "del_tab_key", NULL)) {
		sakura_set_keybind("del_tab_key", DEFAULT_DEL_TAB_KEY);
	}
	sakura.keymap.del_tab_key = sakura_get_keybind("del_tab_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "prev_tab_key", NULL)) {
		sakura_set_keybind("prev_tab_key", DEFAULT_PREV_TAB_KEY);
	}
	sakura.keymap.prev_tab_key = sakura_get_keybind("prev_tab_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "next_tab_key", NULL)) {
		sakura_set_keybind("next_tab_key", DEFAULT_NEXT_TAB_KEY);
	}
	sakura.keymap.next_tab_key = sakura_get_keybind("next_tab_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "copy_key", NULL)) {
		sakura_set_keybind( "copy_key", DEFAULT_COPY_KEY);
	}
	sakura.keymap.copy_key = sakura_get_keybind("copy_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "paste_key", NULL)) {
		sakura_set_keybind("paste_key", DEFAULT_PASTE_KEY);
	}
	sakura.keymap.paste_key = sakura_get_keybind("paste_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "scrollbar_key", NULL)) {
		sakura_set_keybind("scrollbar_key", DEFAULT_SCROLLBAR_KEY);
	}
	sakura.keymap.scrollbar_key = sakura_get_keybind("scrollbar_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "set_tab_name_key", NULL)) {
		sakura_set_keybind("set_tab_name_key", DEFAULT_SET_TAB_NAME_KEY);
	}
	sakura.keymap.set_tab_name_key = sakura_get_keybind("set_tab_name_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "search_key", NULL)) {
		sakura_set_keybind("search_key", DEFAULT_SEARCH_KEY);
	}
	sakura.keymap.search_key = sakura_get_keybind("search_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "increase_font_size_key", NULL)) {
		sakura_set_keybind("increase_font_size_key", DEFAULT_INCREASE_FONT_SIZE_KEY);
	}
	sakura.keymap.increase_font_size_key = sakura_get_keybind("increase_font_size_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "decrease_font_size_key", NULL)) {
		sakura_set_keybind("decrease_font_size_key", DEFAULT_DECREASE_FONT_SIZE_KEY);
	}
	sakura.keymap.decrease_font_size_key = sakura_get_keybind("decrease_font_size_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "fullscreen_key", NULL)) {
		sakura_set_keybind("fullscreen_key", DEFAULT_FULLSCREEN_KEY);
	}
	sakura.keymap.fullscreen_key = sakura_get_keybind("fullscreen_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "set_colorset_accelerator", NULL)) {
		sakura_set_config_integer("set_colorset_accelerator", DEFAULT_SELECT_COLORSET_ACCELERATOR);
	}
	sakura.set_colorset_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "set_colorset_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "icon_file", NULL)) {
		sakura_set_config_string("icon_file", ICON_FILE);
	}
	sakura.icon = g_key_file_get_string(sakura.cfg, cfg_group, "icon_file", NULL);

	/* set default title pattern from config or NULL */
	sakura.tab_default_title = g_key_file_get_string(sakura.cfg, cfg_group, "tab_default_title", NULL);

	/* Use always GTK header bar*/
	g_object_set(gtk_settings_get_default(), "gtk-dialogs-use-header", TRUE, NULL);

	sakura.provider = gtk_css_provider_new();

	sakura.main_window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(sakura.main_window), "sakura");

	/* Default terminal size*/
	sakura.columns = DEFAULT_COLUMNS;
	sakura.rows = DEFAULT_ROWS;

	/* Create notebook and set style */
	sakura.notebook=gtk_notebook_new();
	gtk_notebook_set_scrollable((GtkNotebook*)sakura.notebook, sakura.scrollable_tabs);

	gchar *css = g_strdup_printf(NOTEBOOK_CSS);
	gtk_css_provider_load_from_data(sakura.provider, css, -1, NULL);
	GtkStyleContext *context = gtk_widget_get_style_context(sakura.notebook);
	gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(sakura.provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);

	/* Adding mask, for handle scroll events */
	gtk_widget_add_events(sakura.notebook, GDK_SCROLL_MASK);

	/* Figure out if we have rgba capabilities. FIXME: Is this really needed? */
	GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (sakura.main_window));
	GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
	if (visual != NULL && gdk_screen_is_composited (screen)) {
		gtk_widget_set_visual (GTK_WIDGET (sakura.main_window), visual);
	}

	/* Command line optionsNULL initialization */

	/* Set argv for forked childs. Real argv vector starts at argv[1] because we're
	   using G_SPAWN_FILE_AND_ARGV_ZERO to be able to launch login shells */
	sakura.argv[0]=g_strdup(g_getenv("SHELL"));
	if (option_login) {
		sakura.argv[1]=g_strdup_printf("-%s", g_getenv("SHELL"));
	} else {
		sakura.argv[1]=g_strdup(g_getenv("SHELL"));
	}
	sakura.argv[2]=NULL;

	if (option_title) {
		gtk_window_set_title(GTK_WINDOW(sakura.main_window), option_title);
	}

	if (option_columns) {
		sakura.columns = option_columns;
	}

	if (option_rows) {
		sakura.rows = option_rows;
	}

	/* Add datadir path to icon name and set icon */
	gchar *icon_path; error=NULL;
	if (option_icon) {
		icon_path = g_strdup_printf("%s", option_icon);
	} else {
		icon_path = g_strdup_printf(DATADIR "/pixmaps/%s", sakura.icon);
	}
	gtk_window_set_icon_from_file(GTK_WINDOW(sakura.main_window), icon_path, &error);
	g_free(icon_path); icon_path=NULL;
	if (error) g_error_free(error);

	if (option_font) {
		sakura.font=pango_font_description_from_string(option_font);
	}

	if (option_colorset && option_colorset>0 && option_colorset <= NUM_COLORSETS) {
		sakura.last_colorset=option_colorset;
	}

	/* These options are exclusive */
	if (option_fullscreen) {
		sakura_fullscreen(nullptr, NULL);
	} else if (option_maximize) {
		gtk_window_maximize(GTK_WINDOW(sakura.main_window));
	}

	sakura.label_count=1;
	sakura.fullscreen=FALSE;
	sakura.resized=FALSE;
	sakura.keep_fc=false;
	sakura.externally_modified=false;

	error = nullptr;
	sakura.http_vteregexp=vte_regex_new_for_match(HTTP_REGEXP, strlen(HTTP_REGEXP), 0, &error);
	if (!sakura.http_vteregexp) {
		SAY("http_regexp: %s", error->message);
		g_error_free(error);
	}
	error = nullptr;
	sakura.mail_vteregexp=vte_regex_new_for_match(MAIL_REGEXP, strlen(MAIL_REGEXP), 0, &error);
	if (!sakura.mail_vteregexp) {
		SAY("mail_regexp: %s", error->message);
		g_error_free(error);
	}

	gtk_container_add(GTK_CONTAINER(sakura.main_window), sakura.notebook);

	/* Adding mask to see wheter sakura window is focused or not */
	//gtk_widget_add_events(sakura.main_window, GDK_FOCUS_CHANGE_MASK);
	sakura.focused = true;
	sakura.first_focus = true;
	sakura.faded = false;

	sakura_init_popup();

	g_signal_connect(G_OBJECT(sakura.main_window), "delete_event", G_CALLBACK(sakura_delete_event), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "destroy", G_CALLBACK(sakura_destroy_window), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "key-press-event", G_CALLBACK(Sakura::on_key_press), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "configure-event", G_CALLBACK(sakura_resized_window), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "focus-out-event", G_CALLBACK(sakura_focus_out), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "focus-in-event", G_CALLBACK(sakura_focus_in), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "show", G_CALLBACK(sakura_window_show_event), NULL);
	//g_signal_connect(G_OBJECT(sakura.notebook), "focus-in-event", G_CALLBACK(sakura_notebook_focus_in), NULL);
	g_signal_connect(sakura.notebook, "scroll-event", G_CALLBACK(sakura_notebook_scroll), NULL);

	/* Add initial tabs (1 by default) */
	for (i=0; i<option_ntabs; i++)
		sakura_add_tab();

	sakura_sanitize_working_directory();

}

void Sakura::destroy()
{
	SAY("Destroying sakura");

	/* Delete all existing tabs */
	while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) >= 1) {
		del_tab(-1);
	}

	g_key_file_free(sakura.cfg);

	pango_font_description_free(sakura.font);

	gtk_main_quit();

}

gboolean Sakura::on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	if (event->type!=GDK_KEY_PRESS) return FALSE;

	unsigned int topage = 0;

	gint npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	/* Use keycodes instead of keyvals. With keyvals, key bindings work only in US/ISO8859-1 and similar locales */
	guint keycode = event->hardware_keycode;

	/* Add/delete tab keybinding pressed */
	if ( (event->state & sakura.add_tab_accelerator)==sakura.add_tab_accelerator &&
		 keycode==sakura_tokeycode(sakura.keymap.add_tab_key)) {
		sakura_add_tab();
		return TRUE;
	} else if ( (event->state & sakura.del_tab_accelerator)==sakura.del_tab_accelerator &&
				keycode==sakura_tokeycode(sakura.keymap.del_tab_key) ) {
		/* Delete current tab */
		sakura_close_tab(NULL, NULL);
		return TRUE;
	}

	/* Switch tab keybinding pressed (numbers or next/prev) */
	/* In cases when the user configured accelerators like these ones:
		switch_tab_accelerator=4  for ctrl+next[prev]_tab_key
		move_tab_accelerator=5  for ctrl+shift+next[prev]_tab_key
	   move never works, because switch will be processed first, so it needs to be fixed with the following condition */
	if ( ((event->state & sakura.switch_tab_accelerator) == sakura.switch_tab_accelerator) &&
		 ((event->state & sakura.move_tab_accelerator) != sakura.move_tab_accelerator) ) {

		if ((keycode>=sakura_tokeycode(GDK_KEY_1)) && (keycode<=sakura_tokeycode( GDK_KEY_9))) {

			/* User has explicitly disabled this branch, make sure to propagate the event */
			if(sakura.disable_numbered_tabswitch) return FALSE;

			if      (sakura_tokeycode(GDK_KEY_1) == keycode) topage = 0;
			else if (sakura_tokeycode(GDK_KEY_2) == keycode) topage = 1;
			else if (sakura_tokeycode(GDK_KEY_3) == keycode) topage = 2;
			else if (sakura_tokeycode(GDK_KEY_4) == keycode) topage = 3;
			else if (sakura_tokeycode(GDK_KEY_5) == keycode) topage = 4;
			else if (sakura_tokeycode(GDK_KEY_6) == keycode) topage = 5;
			else if (sakura_tokeycode(GDK_KEY_7) == keycode) topage = 6;
			else if (sakura_tokeycode(GDK_KEY_8) == keycode) topage = 7;
			else if (sakura_tokeycode(GDK_KEY_9) == keycode) topage = 8;
			if (topage <= npages)
				gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), topage);
			return TRUE;
		} else if (keycode==sakura_tokeycode(sakura.keymap.prev_tab_key)) {
			if (gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook))==0) {
				gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), npages-1);
			} else {
				gtk_notebook_prev_page(GTK_NOTEBOOK(sakura.notebook));
			}
			return TRUE;
		} else if (keycode==sakura_tokeycode(sakura.keymap.next_tab_key)) {
			if (gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook))==(npages-1)) {
				gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), 0);
			} else {
				gtk_notebook_next_page(GTK_NOTEBOOK(sakura.notebook));
			}
			return TRUE;
		}
	}

	/* Move tab keybinding pressed */
	if ( ((event->state & sakura.move_tab_accelerator) == sakura.move_tab_accelerator)) {
		if (keycode==sakura_tokeycode(sakura.keymap.prev_tab_key)) {
			sakura_move_tab(BACKWARDS);
			return TRUE;
		} else if (keycode==sakura_tokeycode(sakura.keymap.next_tab_key)) {
			sakura_move_tab(FORWARD);
			return TRUE;
		}
	}

	/* Copy/paste keybinding pressed */
	if ( (event->state & sakura.copy_accelerator)==sakura.copy_accelerator ) {
		if (keycode==sakura_tokeycode(sakura.keymap.copy_key)) {
			sakura_copy(NULL, NULL);
			return TRUE;
		} else if (keycode==sakura_tokeycode(sakura.keymap.paste_key)) {
			sakura_paste(NULL, NULL);
			return TRUE;
		}
	}

	/* Show scrollbar keybinding pressed */
	if ( (event->state & sakura.scrollbar_accelerator)==sakura.scrollbar_accelerator ) {
		if (keycode==sakura_tokeycode(sakura.keymap.scrollbar_key)) {
			sakura_show_scrollbar(NULL, NULL);
			return TRUE;
		}
	}

	/* Set tab name keybinding pressed */
	if ( (event->state & sakura.set_tab_name_accelerator)==sakura.set_tab_name_accelerator ) {
		if (keycode==sakura_tokeycode(sakura.keymap.set_tab_name_key)) {
			sakura_set_name_dialog(NULL, NULL);
			return TRUE;
		}
	}

	/* Search keybinding pressed */
	if ( (event->state & sakura.search_accelerator)==sakura.search_accelerator ) {
		if (keycode==sakura_tokeycode(sakura.keymap.search_key)) {
			sakura_search_dialog(NULL, NULL);
			return TRUE;
		}
	}

	/* Increase/decrease font size keybinding pressed */
	if ( (event->state & sakura.font_size_accelerator)==sakura.font_size_accelerator ) {
		if (keycode==sakura_tokeycode(sakura.keymap.increase_font_size_key)) {
			sakura_increase_font(NULL, NULL);
			return TRUE;
		} else if (keycode==sakura_tokeycode(sakura.keymap.decrease_font_size_key)) {
			sakura_decrease_font(NULL, NULL);
			return TRUE;
		}
	}

	/* F11 (fullscreen) pressed */
	if (keycode==sakura_tokeycode(sakura.keymap.fullscreen_key)){
		sakura_fullscreen(NULL, NULL);
		return TRUE;
	}

	/* Change in colorset */
	if ( (event->state & sakura.set_colorset_accelerator)==sakura.set_colorset_accelerator ) {
		int i;
		for(i=0; i<NUM_COLORSETS; i++) {
			if (keycode==sakura_tokeycode(sakura.keymap.set_colorset_keys[i])){
				sakura_set_colorset(i);
				return TRUE;
			}
		}
	}
	return FALSE;
}


/* Delete the notebook tab passed as a parameter */
void Sakura::del_tab(gint page)
{
	gint npages;
	auto *term = sakura_get_page_term(sakura, page);
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	/* When there's only one tab use the shell title, if provided */
	if (npages == 2) {
		term = sakura_get_page_term(sakura, 0);
		const char * title = vte_terminal_get_window_title(VTE_TERMINAL(term->vte));
		if (title)
			gtk_window_set_title(GTK_WINDOW(sakura.main_window), title);
	}

	term = sakura_get_page_term(sakura, page);

	/* Do the first tab checks BEFORE deleting the tab, to ensure correct
	 * sizes are calculated when the tab is deleted */
	if (npages == 2) {
		if (sakura.first_tab) {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), TRUE);
		} else {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), FALSE);
		}
		sakura.keep_fc = true;
	}

	gtk_widget_hide(term->hbox);
	gtk_notebook_remove_page(GTK_NOTEBOOK(sakura.notebook), page);

	/* Find the next page, if it exists, and grab focus */
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) > 0) {
		page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
		term = sakura_get_page_term(sakura, page);
		gtk_widget_grab_focus(term->vte);
	}
}
