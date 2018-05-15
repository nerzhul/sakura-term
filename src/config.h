#pragma once

#include <string>
#include <glib.h>
#include <pango/pango.h>
#include <vte/vte.h>
#include "palettes.h"

#define NUM_COLORSETS 6

struct SakuraKeyMap {
	gint add_tab_key;
	gint del_tab_key;
	gint prev_tab_key;
	gint next_tab_key;
	gint copy_key;
	gint paste_key;
	gint scrollbar_key;
	gint set_tab_name_key;
	gint search_key;
	gint fullscreen_key;
	gint increase_font_size_key;
	gint decrease_font_size_key;
	gint set_colorset_keys[NUM_COLORSETS];
};

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
	std::string palette_str = "solarized_dark";
	const GdkRGBA *palette = solarized_dark_palette;

	gint last_colorset = 1;
	gint scroll_lines = 4096;

	bool first_tab = false;
	bool show_scrollbar = false;
	bool show_closebutton = true;
	bool tabs_on_bottom = false;
	bool less_questions = false;
	bool disable_numbered_tabswitch = false; /* For disabling direct tabswitching key */
	bool use_fading = false;
	bool scrollable_tabs = true;
	bool urgent_bell = true;
	bool audible_bell = true;
	bool blinking_cursor = false;
	bool stop_tab_cycling_at_end_tabs = false;
	bool allow_bold = true;

	gint add_tab_accelerator = (GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	gint del_tab_accelerator = (GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	gint switch_tab_accelerator = (GDK_CONTROL_MASK);
	gint move_tab_accelerator = (GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	gint copy_accelerator = (GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	gint scrollbar_accelerator = (GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	gint open_url_accelerator = (GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	gint font_size_accelerator = (GDK_CONTROL_MASK);
	gint set_tab_name_accelerator = (GDK_CONTROL_MASK | GDK_SHIFT_MASK);
	gint search_accelerator = (GDK_CONTROL_MASK | GDK_SHIFT_MASK);

	VteCursorShape cursor_type = VTE_CURSOR_SHAPE_BLOCK;
	std::string word_chars = "-,./?%&#_~:";  /* Exceptions for word selection */

	SakuraKeyMap keymap;
private:
	std::string m_file;
};
