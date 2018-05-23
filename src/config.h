#pragma once

#include <string>
#include <glib.h>
#include <pango/pango.h>
#include <vte/vte.h>
#include <yaml-cpp/node/node.h>
#include "palettes.h"

#define NUM_COLORSETS 6

// @TODO remove this when finished to migrate to Config object
guint sakura_get_keybind(const gchar *key);

struct SakuraKeyMap {
	gint add_tab_key = GDK_KEY_T;
	gint del_tab_key = GDK_KEY_W;
	gint prev_tab_key = GDK_KEY_Left;
	gint next_tab_key = GDK_KEY_Right;
	gint copy_key = GDK_KEY_C;
	gint paste_key = GDK_KEY_V;
	gint scrollbar_key = GDK_KEY_S;
	gint set_tab_name_key = GDK_KEY_N;
	gint search_key = GDK_KEY_F;
	gint fullscreen_key = GDK_KEY_F11;
	gint increase_font_size_key = GDK_KEY_plus;
	gint decrease_font_size_key = GDK_KEY_minus;
	std::array<gint, NUM_COLORSETS> set_colorset_keys;
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
	gint set_colorset_accelerator = (GDK_CONTROL_MASK | GDK_SHIFT_MASK);

	VteCursorShape cursor_type = VTE_CURSOR_SHAPE_BLOCK;
	std::string word_chars = "-,./?%&#_~:";  /* Exceptions for word selection */
	std::string icon = "terminal-tango.svg";

	const std::string &get_background_image() const { return m_background_image; }
	double get_background_alpha() const { return m_background_alpha; }

	SakuraKeyMap keymap;
private:
	void loadKeymap(const YAML::Node &keymap_node);
	void loadColorset(const YAML::Node *colorset_node, uint8_t index);

	std::string m_background_image;
	double m_background_alpha = 0.9;

	GFile *m_monitored_file = nullptr;
	std::string m_file;
};
