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
	if (font) {
		pango_font_description_free(font);
	}
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
			tabs_on_bottom = config["tabs_on_bottom"].as<bool>();
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
