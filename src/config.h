#pragma once

#include <string>
#include <glib.h>
#include <pango/pango.h>

class Config
{
public:
	Config();
	~Config();

	void write();

	bool read();

	void monitor();

	// @TODO make that private
	PangoFontDescription *font = nullptr;

	gint last_colorset = 1;
	gint scroll_lines = 4096;

	bool first_tab = false;
	bool show_scrollbar = false;
	bool show_closebutton = true;
	bool tabs_on_bottom = false;

private:
	std::string m_file;
};
