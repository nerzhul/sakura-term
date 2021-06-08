#include "notebook.h"
#include "gettext.h"
#include "terminal.h"
#include "sakura.h"
#include "window.h"
#include "sakuraold.h"

SakuraNotebook::SakuraNotebook(const Config *cfg) : m_cfg(cfg)
{
	set_scrollable(cfg->scrollable_tabs);

	/* Adding mask, for handle scroll events */
	add_events(Gdk::SCROLL_MASK);

	signal_scroll_event().connect(sigc::mem_fun(*this, &SakuraNotebook::on_scroll_event));
	signal_page_removed().connect(sigc::mem_fun(*this, &SakuraNotebook::on_page_removed_event));
}

SakuraNotebook::~SakuraNotebook()
{
	/* Delete all existing tabs */
	while (get_n_pages() >= 1) {
		del_tab(-1);
	}
}

bool SakuraNotebook::on_scroll_event(GdkEventScroll *scroll)
{
	int page = get_current_page();
	int npages = get_n_pages();

	switch (scroll->direction) {
	case GDK_SCROLL_DOWN: {
		if (m_cfg->stop_tab_cycling_at_end_tabs == 1) {
			set_current_page(--page >= 0 ? page : 0);
		} else {
			set_current_page(--page >= 0 ? page : npages - 1);
		}
		break;
	}
	case GDK_SCROLL_UP: {
		if (m_cfg->stop_tab_cycling_at_end_tabs == 1) {
			set_current_page(++page < npages ? page : npages - 1);
		} else {
			set_current_page(++page < npages ? page : 0);
		}
		break;
	}
	case GDK_SCROLL_LEFT:
	case GDK_SCROLL_RIGHT:
	case GDK_SCROLL_SMOOTH:
		break;
	}

	return false;
}

void SakuraNotebook::on_page_removed_event(Gtk::Widget *, guint)
{
	if (get_n_pages() == 1) {
		/* If the first tab is disabled, window size changes and we need
		 * to recalculate its size */
		sakura->set_size();
	}
}

void SakuraNotebook::move_tab(gint direction)
{
	gint page = get_current_page();
	gint n_pages = get_n_pages();
	auto child = get_nth_page(page);

	if (direction == FORWARD) {
		if (page != n_pages - 1)
			reorder_child(*child, page + 1);
	} else {
		if (page != 0)
			reorder_child(*child, page - 1);
	}
}

/* Find the notebook page for the vte terminal passed as a parameter */
gint SakuraNotebook::find_tab(VteTerminal *vte_term)
{
	gint n_pages = get_n_pages();
	gint matched_page = -1;
	gint page = 0;

	do {
		auto term = get_tab_term(page);
		if ((VteTerminal *)term->vte == vte_term) {
			matched_page = page;
		}
		page++;
	} while (page < n_pages);

	return (matched_page);
}

void SakuraNotebook::show_scrollbar()
{
	sakura->keep_fc = 1;

	gint n_pages = get_n_pages();
	auto term = get_tab_term(get_current_page());

	if (!g_key_file_get_boolean(sakura->cfg, cfg_group, "scrollbar", NULL)) {
		sakura->config.show_scrollbar = true;
		sakura_set_config_boolean("scrollbar", TRUE);
	} else {
		sakura->config.show_scrollbar = false;
		sakura_set_config_boolean("scrollbar", FALSE);
	}

	/* Toggle/Untoggle the scrollbar for all tabs */
	for (int i = (n_pages - 1); i >= 0; i--) {
		term = get_tab_term(i);
		if (!sakura->config.show_scrollbar)
			gtk_widget_hide(term->scrollbar);
		else
			gtk_widget_show(term->scrollbar);
	}
	sakura->set_size();
}

void SakuraNotebook::close_tab()
{
	gint page = get_current_page();
	gint npages = get_n_pages();
	auto term = get_tab_term(page);

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	/* Check if there are running processes for this tab. Use tcgetpgrp to compare to the shell
	 * PGID */
	auto pgid = tcgetpgrp(vte_pty_get_fd(vte_terminal_get_pty(VTE_TERMINAL(term->vte))));

	if ((pgid != -1) && (pgid != term->pid) && (!sakura->config.less_questions)) {
		auto dialog = gtk_message_dialog_new(sakura->main_window->gobj(), GTK_DIALOG_MODAL,
				GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
				_("There is a running process in this terminal.\n\nDo you really "
				  "want to close it?"));

		auto response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		if (response == GTK_RESPONSE_YES) {
			del_tab(page, true);
		}
	} else {
		del_tab(page, true);
	}
}

/* Delete the notebook tab passed as a parameter */
void SakuraNotebook::del_tab(gint page, bool exit_if_needed)
{
	auto term = get_tab_term(page);
	gint npages = get_n_pages();

	/* When there's only one tab use the shell title, if provided */
	if (npages == 2) {
		term = get_tab_term(0);
		const char *title = vte_terminal_get_window_title(VTE_TERMINAL(term->vte));
		if (title) {
			sakura->main_window->set_title(title);
		}
	}

	term = get_tab_term(page);

	/* Do the first tab checks BEFORE deleting the tab, to ensure correct
	 * sizes are calculated when the tab is deleted */
	if (npages == 2) {
		set_show_tabs(sakura->config.first_tab);
		sakura->keep_fc = true;
	}

	term->hbox.hide();
	remove_page(page);

	/* Find the next page, if it exists, and grab focus */
	if (get_n_pages() > 0) {
		term = get_current_tab_term();
		gtk_widget_grab_focus(term->vte);
	}

	if (exit_if_needed) {
		if (get_n_pages() == 0)
			sakura->destroy(nullptr);
	}
}

Terminal *SakuraNotebook::get_tab_term(gint page_id)
{
	return (Terminal *)g_object_get_qdata(
			G_OBJECT(get_nth_page(page_id)->gobj()), term_data_id);
}

Terminal *SakuraNotebook::get_current_tab_term()
{
	return get_tab_term(get_current_page());
}