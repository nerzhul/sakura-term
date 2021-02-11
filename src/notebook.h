#pragma once

#include <gtkmm.h>
#include "config.h"

class SakuraNotebook : public Gtk::Notebook
{
public:
	SakuraNotebook(const Config *cfg);
	~SakuraNotebook();

	bool on_scroll_event(GdkEventScroll *scroll);
	void on_page_removed_event(Gtk::Widget *, guint);

	gint find_tab(VteTerminal *term);
	void move_tab(gint direction);
private:

	const Config *m_cfg = nullptr;
};
