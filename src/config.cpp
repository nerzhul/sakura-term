#include "config.h"
#include "sakuraold.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <iostream>
#include <yaml-cpp/yaml.h>

#define DEFAULT_CONFIGFILE "sakura.yml"
#define DEFAULT_FONT "Ubuntu Mono,monospace 12"

Config::Config()
{
	gchar *configdir = g_build_filename(g_get_user_config_dir(), "sakura", NULL);
	if (!g_file_test(g_get_user_config_dir(), G_FILE_TEST_EXISTS))
		g_mkdir(g_get_user_config_dir(), 0755);
	if (!g_file_test(configdir, G_FILE_TEST_EXISTS))
		g_mkdir(configdir, 0755);
	if (option_config_file) {
		gchar *tmpfile = g_build_filename(configdir, option_config_file, NULL);
		m_file = tmpfile;
		g_free(tmpfile);
	} else {
		/* Use more standard-conforming path for config files, if available. */
		gchar *tmpfile = g_build_filename(configdir, DEFAULT_CONFIGFILE, NULL);
		m_file = tmpfile;
		g_free(tmpfile);
	}
	g_free(configdir);

	font = pango_font_description_from_string(DEFAULT_FONT);

	std::cout << "Configuration file set to " << m_file << std::endl;
}

Config::~Config()
{
}

void Config::write()
{
	// @TODO
}

bool Config::read()
{
	try {
		YAML::Node config = YAML::LoadFile(m_file);

		if (config["last_colorset"]) {
			last_colorset = config["last_colorset"].as<gint>();
		}

		if (config["scroll_lines"]) {
			scroll_lines = config["scroll_lines"].as<gint>();
		}

		if (config["font"]) {
			if (font) {
				pango_font_description_free(font);
			}
			font = pango_font_description_from_string(
					config["font"].as<std::string>().c_str());
		}

		if (config["show_always_first_tab"]) {
			first_tab = config["show_always_first_tab"].as<bool>();
		}

		if (config["scrollbar"]) {
			show_scrollbar = config["scrollbar"].as<bool>();
		}

		if (config["closebutton"]) {
			show_closebutton = config["closebutton"].as<bool>();
		}

		if (config["tabs_on_bottom"]) {
			less_questions = config["tabs_on_bottom"].as<bool>();
		}

		if (config["disable_numbered_tabswitch"]) {
			disable_numbered_tabswitch = config["disable_numbered_tabswitch"].as<bool>();
		}

		if (config["use_fading"]) {
			use_fading = config["use_fading"].as<bool>();
		}

		if (config["scrollable_tabs"]) {
			scrollable_tabs = config["scrollable_tabs"].as<bool>();
		}

		if (config["urgent_bell"]) {
			urgent_bell = config["urgent_bell"].as<bool>();
		}

		if (config["audible_bell"]) {
			audible_bell = config["audible_bell"].as<bool>();
		}

		if (config["blinking_cursor"]) {
			blinking_cursor = config["blinking_cursor"].as<bool>();
		}

		if (config["stop_tab_cycling_at_end_tabs"]) {
			stop_tab_cycling_at_end_tabs = config["stop_tab_cycling_at_end_tabs"].as<bool>();
		}

		if (config["allow_bold"]) {
			allow_bold = config["allow_bold"].as<bool>();
		}

		if (config["cursor_type"]) {
			cursor_type = (VteCursorShape) config["cursor_type"].as<gint>();
		}

		if (config["word_chars"]) {
			word_chars = config["word_chars"].as<std::string>();
		}

		if (config["palette"]) {
			palette_str = config["palette"].as<std::string>();
			if (palette_str == "linux") {
				palette = linux_palette;
			} else if (palette_str == "gruvbox") {
				palette = gruvbox_palette;
			} else if (palette_str == "xterm") {
				palette = xterm_palette;
			} else if (palette_str == "rxvt") {
				palette = rxvt_palette;
			} else if (palette_str == "tango") {
				palette = tango_palette;
			} else if (palette_str == "solarized_dark") {
				palette = solarized_dark_palette;
			} else {
				palette = solarized_light_palette;
			}
		}

		if (config["add_tab_accelerator"]) {
			add_tab_accelerator = config["add_tab_accelerator"].as<gint>();
		}

		if (config["del_tab_accelerator"]) {
			del_tab_accelerator = config["del_tab_accelerator"].as<gint>();
		}

		if (config["switch_tab_accelerator"]) {
			switch_tab_accelerator = config["switch_tab_accelerator"].as<gint>();
		}

		if (config["move_tab_accelerator"]) {
			move_tab_accelerator = config["move_tab_accelerator"].as<gint>();
		}

		if (config["copy_accelerator"]) {
			copy_accelerator = config["copy_accelerator"].as<gint>();
		}

		if (config["scrollbar_accelerator"]) {
			scrollbar_accelerator = config["scrollbar_accelerator"].as<gint>();
		}

		if (config["open_url_accelerator"]) {
			open_url_accelerator = config["open_url_accelerator"].as<gint>();
		}

		if (config["font_size_accelerator"]) {
			font_size_accelerator = config["font_size_accelerator"].as<gint>();
		}

		if (config["set_tab_name_accelerator"]) {
			set_tab_name_accelerator = config["set_tab_name_accelerator"].as<gint>();
		}

		if (config["search_accelerator"]) {
			search_accelerator = config["search_accelerator"].as<gint>();
		}
	} catch (const YAML::BadFile &e) {
		std::cout << "Failed to read configuration file: " << e.what()
			  << ", using defaults" << std::endl;
	}

	return true;
}

void Config::monitor()
{
	/* Add GFile monitor to control file external changes */
	GFile *cfgfile = g_file_new_for_path(m_file.c_str());
	GFileMonitor *mon_cfgfile =
			g_file_monitor_file(cfgfile, (GFileMonitorFlags)0, NULL, NULL);
	g_signal_connect(G_OBJECT(mon_cfgfile), "changed",
			G_CALLBACK(sakura_conf_changed), NULL);
}
