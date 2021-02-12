#include "terminal.h"
#include "sakuraold.h"
#include <iostream>
#include <libintl.h>
#include <glib.h>
#include <glib/gstdio.h>

gchar *Terminal::tab_default_title = nullptr;

Terminal::Terminal():
	hbox(Gtk::ORIENTATION_HORIZONTAL, 0)
{
	gchar *_label_text = _("Terminal %d");
	/* appling tab title pattern from config
	 * (https://answers.launchpad.net/sakura/+question/267951) */
	if (tab_default_title != NULL) {
		_label_text = tab_default_title;
		label_set_byuser = true;
	}

	label_text = g_strdup_printf(_label_text, sakura->label_count++);
	label = Gtk::Label(label_text);

	/* Create new vte terminal, scrollbar, and pack it */
	vte = vte_terminal_new();
	scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL,
		gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte)));

	gtk_box_pack_start(GTK_BOX(hbox.gobj()), vte, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox.gobj()), scrollbar, FALSE, FALSE, 0);

	colorset = sakura->config.last_colorset - 1;

	label.set_ellipsize(Pango::ELLIPSIZE_END);
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

/* Retrieve the cwd of the specified term page.
 * Original function was from terminal-screen.c of gnome-terminal, copyright (C) 2001 Havoc
 * Pennington Adapted by Hong Jen Yee, non-linux shit removed by David Gómez */
char *Terminal::get_cwd()
{
	char *cwd = NULL;

	if (pid >= 0) {
		char *file, *buf;
		struct stat sb;
		int len;

		file = g_strdup_printf("/proc/%d/cwd", pid);

		if (g_stat(file, &sb) == -1) {
			g_free(file);
			return cwd;
		}

		buf = (char *)malloc(sb.st_size + 1);

		if (buf == NULL) {
			g_free(file);
			return cwd;
		}

		len = readlink(file, buf, sb.st_size + 1);

		if (len > 0 && buf[0] == '/') {
			buf[len] = '\0';
			cwd = g_strdup(buf);
		}

		g_free(buf);
		g_free(file);
	}

	return cwd;
}