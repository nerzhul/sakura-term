#include <clocale>
#include <libintl.h>
#include <glib.h>
#include <gtk/gtk.h>
#include "gettext.h"
#include "sakuraold.h"

int
main(int argc, char **argv)
{
	gchar *localedir;
	int i; int n;
	char **nargv; int nargc;
	gboolean have_e;

	/* Localization */
	std::setlocale(LC_ALL, "");
	localedir=g_strdup_printf("%s/locale", DATADIR);
	textdomain(GETTEXT_PACKAGE);
	bindtextdomain(GETTEXT_PACKAGE, localedir);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	g_free(localedir);

	/* Rewrites argv to include a -- after the -e argument this is required to make
	 * sure GOption doesn't grab any arguments meant for the command being called */

	/* Initialize nargv */
	nargv = (char**)calloc((argc+1), sizeof(char*));
	n=0; nargc=argc;
	have_e=FALSE;

	for(i=0; i<argc; i++) {
		if(!have_e && g_strcmp0(argv[i],"-e") == 0)
		{
			nargv[n]="-e";
			n++;
			nargv[n]="--";
			nargc++;
			have_e = TRUE;
		} else {
			nargv[n]=g_strdup(argv[i]);
		}
		n++;
	}

	/* Options parsing */
	GError *error=NULL;
	GOptionContext *context; GOptionGroup *option_group;

	context = g_option_context_new (_("- vte-based terminal emulator"));
	option_group = gtk_get_option_group(TRUE);
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_group_set_translation_domain(option_group, GETTEXT_PACKAGE);
	g_option_context_add_group (context, option_group);
	if (!g_option_context_parse (context, &nargc, &nargv, &error)) {
		fprintf(stderr, "%s\n", error->message);
		g_error_free(error);
		exit(1);
	}

	g_option_context_free(context);

	if (option_workdir && chdir(option_workdir)) {
		fprintf(stderr, _("Cannot change working directory\n"));
		exit(1);
	}

	if (option_version) {
		fprintf(stderr, _("sakura version is %s\n"), VERSION);
		exit(1);
	}

	if (option_ntabs <= 0) {
		option_ntabs=1;
	}

	/* Init stuff */
	gtk_init(&nargc, &nargv); g_strfreev(nargv);
	sakura_init();

	/* Add initial tabs (1 by default) */
	for (i=0; i<option_ntabs; i++)
		sakura_add_tab();

	sakura_sanitize_working_directory();

	gtk_main();

	return 0;
}
