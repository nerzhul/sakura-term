#include "terminal.h"
#include "sakuraold.h"
#include <iostream>
#include <libintl.h>

gchar *Terminal::tab_default_title = nullptr;

Terminal::Terminal()
{
	gchar *_label_text = _("Terminal %d");
	/* appling tab title pattern from config
	 * (https://answers.launchpad.net/sakura/+question/267951) */
	if (tab_default_title != NULL) {
		_label_text = tab_default_title;
		label_set_byuser = true;
	}

	label_text = g_strdup_printf(_label_text, sakura->label_count++);
	label = gtk_label_new(label_text);

	/* Create new vte terminal, scrollbar, and pack it */
	vte = vte_terminal_new();
	scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL,
		gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte)));
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vte, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, FALSE, 0);

	colorset = sakura->config.last_colorset - 1;

	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
}

Terminal::~Terminal()
{
	if (bg_image) {
		g_clear_object(&bg_image);
	}

	if (label_text) {
		g_free(label_text);
	}
}

/**
 * Called when terminal is free by GTK notebook qdata removal
 * @param term
 */
void Terminal::free(Terminal *term)
{
	delete term;
}
