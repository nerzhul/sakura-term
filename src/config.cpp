#include "config.h"
#include "sakuraold.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <iostream>
#include <yaml-cpp/yaml.h>
#include <cassert>
#include <filesystem>

namespace fs = std::filesystem;

#define DEFAULT_CONFIGFILE "sakura.yml"
#define DEFAULT_FONT "Ubuntu Mono,monospace 12"
/* make this an array instead of #defines to get a compile time
 * error instead of a runtime if NUM_COLORSETS changes */
static int cs_keys[NUM_COLORSETS] = {
		GDK_KEY_F1, GDK_KEY_F2, GDK_KEY_F3, GDK_KEY_F4, GDK_KEY_F5, GDK_KEY_F6};

Config::Config()
{
	gchar *configdir = g_build_filename(g_get_user_config_dir(), "sakura", NULL);
	const std::string user_config_dir(configdir);
	if (!fs::exists(user_config_dir)) {
		fs::create_directories(user_config_dir);
		fs::permissions(user_config_dir, fs::perms::owner_all);
	}

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
	if (m_monitored_file) {
		g_object_unref(m_monitored_file);
	}

	if (font) {
		// it seems invalid to free with that method (reported by valgrind)
		// pango_font_description_free(font);
	}

}

void Config::write()
{
	// @TODO
}

bool Config::read()
{
	if (!fs::exists(m_file)) {
		std::cout << "Unable to find local configuration file, loading defaults."
			   << std::endl;
		loadDefaults();
		return true;
	}

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
			disable_numbered_tabswitch =
					config["disable_numbered_tabswitch"].as<bool>();
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
			stop_tab_cycling_at_end_tabs =
					config["stop_tab_cycling_at_end_tabs"].as<bool>();
		}

		if (config["allow_bold"]) {
			allow_bold = config["allow_bold"].as<bool>();
		}

		if (config["cursor_type"]) {
			cursor_type = (VteCursorShape)config["cursor_type"].as<gint>();
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

		if (config["set_colorset_accelerator"]) {
			set_colorset_accelerator = config["set_colorset_accelerator"].as<gint>();
		}

		if (config["search_accelerator"]) {
			search_accelerator = config["search_accelerator"].as<gint>();
		}

		if (config["icon"]) {
			icon = config["icon"].as<std::string>();
		}

		if (config["background_image"]) {
			m_background_image = config["background_image"].as<std::string>();
		}

		if (config["background_alpha"]) {
			m_background_alpha = config["background_alpha"].as<double>();
			if (m_background_alpha < 0.0 || m_background_alpha > 1.0) {
				std::cerr << "Invalid background alpha value " << m_background_alpha
					<< ", reseting to " << (config["background_image"] ? 0.9 : 1.0)
					<< std::endl;
				m_background_alpha = (config["background_image"] ? 0.9 : 1.0);
			}
		}

		if (config["keymap"]) {
			loadKeymap(config["keymap"]);
		}

		for (uint8_t i = 0; i < NUM_COLORSETS; i++) {
			char temp_name[64];
			memset(temp_name, 0, sizeof(temp_name));
			sprintf(temp_name, "colorset%d", i + 1);
			// config[temp_name] can be nil, it will be handled in the function
			if (config[temp_name]) {
				const YAML::Node &node = config[temp_name];
				loadColorset(&node, i);
			} else {
				loadColorset(nullptr, i);
			}
		}

	} catch (const YAML::BadFile &e) {
		std::cout << "Failed to read configuration file: " << e.what() << ", using defaults"
			  << std::endl;
		loadDefaults();
	}

	return true;
}

void Config::loadDefaults()
{
	for (uint8_t i = 0; i < NUM_COLORSETS; i++) {
		char temp_name[64];
		memset(temp_name, 0, sizeof(temp_name));
		sprintf(temp_name, "colorset%d", i + 1);
		loadColorset(nullptr, i);
	}
}

guint sakura_get_keybind(const gchar *key)
{
	gchar *value;
	guint retval = GDK_KEY_VoidSymbol;

	value = g_key_file_get_string(sakura->cfg, cfg_group, key, NULL);
	if (value != NULL) {
		retval = gdk_keyval_from_name(value);
		g_free(value);
	}

	/* For backwards compatibility with integer values */
	/* If gdk_keyval_from_name fail, it seems to be integer value*/
	if ((retval == GDK_KEY_VoidSymbol) || (retval == 0)) {
		retval = (guint)g_key_file_get_integer(sakura->cfg, cfg_group, key, NULL);
	}

	/* Always use uppercase value as keyval */
	return gdk_keyval_to_upper(retval);
}

void Config::loadKeymap(const YAML::Node &keymap_node)
{
	if (keymap_node["add_tab"]) {
		keymap.add_tab_key = sakura_get_keybind(
				keymap_node["add_tab"].as<std::string>().c_str());
	}

	if (keymap_node["del_tab"]) {
		keymap.del_tab_key = sakura_get_keybind(
				keymap_node["del_tab"].as<std::string>().c_str());
	}

	if (keymap_node["prev_tab"]) {
		keymap.prev_tab_key = sakura_get_keybind(
				keymap_node["prev_tab"].as<std::string>().c_str());
	}

	if (keymap_node["next_tab"]) {
		keymap.next_tab_key = sakura_get_keybind(
				keymap_node["next_tab"].as<std::string>().c_str());
	}

	if (keymap_node["copy"]) {
		keymap.copy_key = sakura_get_keybind(
				keymap_node["copy_key"].as<std::string>().c_str());
	}

	if (keymap_node["paste"]) {
		keymap.paste_key =
				sakura_get_keybind(keymap_node["paste"].as<std::string>().c_str());
	}

	if (keymap_node["scrollbar"]) {
		keymap.scrollbar_key = sakura_get_keybind(
				keymap_node["scrollbar"].as<std::string>().c_str());
	}

	if (keymap_node["set_tab_name"]) {
		keymap.set_tab_name_key = sakura_get_keybind(
				keymap_node["set_tab_name"].as<std::string>().c_str());
	}

	if (keymap_node["search"]) {
		keymap.search_key =
				sakura_get_keybind(keymap_node["search"].as<std::string>().c_str());
	}

	if (keymap_node["increase_font_size"]) {
		keymap.increase_font_size_key = sakura_get_keybind(
				keymap_node["increase_font_size"].as<std::string>().c_str());
	}

	if (keymap_node["decrease_font_size"]) {
		keymap.decrease_font_size_key = sakura_get_keybind(
				keymap_node["decrease_font_size"].as<std::string>().c_str());
	}

	if (keymap_node["fullscreen"]) {
		keymap.fullscreen_key = sakura_get_keybind(
				keymap_node["fullscreen"].as<std::string>().c_str());
	}
}

void Config::loadColorset(const YAML::Node *colorset_node, uint8_t index)
{
	if (colorset_node && (*colorset_node)["fore"]) {
		gdk_rgba_parse(&sakura->forecolors[index], (*colorset_node)["fore"].as<std::string>().c_str());
	} else {
		gdk_rgba_parse(&sakura->forecolors[index], "rgb(192,192,192)");
	}

	if (colorset_node && (*colorset_node)["back"]) {
		gdk_rgba_parse(&sakura->backcolors[index], (*colorset_node)["back"].as<std::string>().c_str());
	} else {
		gdk_rgba_parse(&sakura->backcolors[index], "rgba(0,0,0,1)");
	}

	if (colorset_node && (*colorset_node)["curs"]) {
		gdk_rgba_parse(&sakura->curscolors[index], (*colorset_node)["curs"].as<std::string>().c_str());
	} else {
		gdk_rgba_parse(&sakura->curscolors[index], "rgb(255,255,255)");
	}

	if (colorset_node && (*colorset_node)["key"]) {
		sakura->config.keymap.set_colorset_keys[index] =
				sakura_get_keybind(colorset_node->Tag().c_str());
	} else {
		sakura->config.keymap.set_colorset_keys[index] = cs_keys[index];
	}
}

void Config::monitor()
{
	assert(m_monitored_file == nullptr);
	/* Add GFile monitor to control file external changes */
	m_monitored_file = g_file_new_for_path(m_file.c_str());
	GFileMonitor *mon_cfgfile = g_file_monitor_file(m_monitored_file, (GFileMonitorFlags)0, NULL, NULL);
	g_signal_connect(G_OBJECT(mon_cfgfile), "changed", G_CALLBACK(sakura_conf_changed), NULL);
}
