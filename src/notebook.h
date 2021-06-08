#pragma once

#include <gtkmm.h>
#include "config.h"

class Terminal;

class SakuraNotebook : public Gtk::Notebook
{
public:
	SakuraNotebook(const Config *cfg);
	~SakuraNotebook();

	bool on_scroll_event(GdkEventScroll *scroll);
	void on_page_removed_event(Gtk::Widget *, guint);

	gint find_tab(VteTerminal *term);
	void move_tab(gint direction);
	void close_tab();
	void del_tab(gint page, bool exit_if_needed = false);

	Terminal *get_tab_term(gint page_id);
	Terminal *get_current_tab_term();
	void show_scrollbar();

private:
	const Config *m_cfg = nullptr;
};
