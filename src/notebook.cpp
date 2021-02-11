#include "notebook.h"
#include "terminal.h"
#include "sakuraold.h"

SakuraNotebook::SakuraNotebook(const Config *cfg) :
	m_cfg(cfg)
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
		sakura->del_tab(-1);
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
		sakura_set_size();
	}
}

/* Find the notebook page for the vte terminal passed as a parameter */
gint SakuraNotebook::find_tab(VteTerminal *vte_term)
{
	gint n_pages = get_n_pages();
	gint matched_page = -1;
	gint page = 0;

	do {
		auto *term = sakura->get_page_term(page);
		if ((VteTerminal *)term->vte == vte_term) {
			matched_page = page;
		}
		page++;
	} while (page < n_pages);

	return (matched_page);
}
