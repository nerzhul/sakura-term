#include "config.h"
#include "sakuraold.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <iostream>

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

	std::cout << "Configuration file set to " << m_file << std::endl;
}

void Config::write()
{
	// @TODO
}

bool Config::read()
{
	// @TODO
	return false;
}

void Config::monitor()
{
	/* Add GFile monitor to control file external changes */
	GFile *cfgfile = g_file_new_for_path(m_file.c_str());
	GFileMonitor *mon_cfgfile = g_file_monitor_file (cfgfile, (GFileMonitorFlags) 0, NULL, NULL);
	g_signal_connect(G_OBJECT(mon_cfgfile), "changed", G_CALLBACK(sakura_conf_changed), NULL);
}
