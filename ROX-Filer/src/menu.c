/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@users.sourceforge.net>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* menu.c - code for handling the popup menu */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <gtk/gtk.h>

#include "global.h"

#include "menu.h"
#include "run.h"
#include "action.h"
#include "filer.h"
#include "pixmaps.h"
#include "type.h"
#include "support.h"
#include "gui_support.h"
#include "options.h"
#include "choices.h"
#include "gtksavebox.h"
#include "mount.h"
#include "minibuffer.h"
#include "i18n.h"
#include "main.h"
#include "pinboard.h"
#include "dir.h"

#define C_ "<control>"

typedef void (*ActionFn)(GSList *paths, char *dest_dir, char *leaf);

GtkAccelGroup	*filer_keys;
GtkAccelGroup	*pinboard_keys;

GtkWidget *popup_menu = NULL;		/* Currently open menu */

static gint updating_menu = 0;		/* Non-zero => ignore activations */

/* Options */
static GtkWidget *xterm_here_entry;
char *xterm_here_value;

/* TRUE if we selected an icon automatically when the menu was opened */
static gboolean pin_temp_item_selected;

/* Static prototypes */

static void save_menus(void);
static void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data);
static void pin_menu_closed(GtkWidget *widget);
static void menu_closed(GtkWidget *widget);
static void items_sensitive(gboolean state);
static char *load_xterm_here(char *data);
static void savebox_show(guchar *title, guchar *path, MaskedPixmap *image,
		gboolean (*callback)(guchar *current, guchar *new));
static gint save_to_file(GtkSavebox *savebox, guchar *pathname);
static GList *list_paths(FilerWindow *filer_window);
static void free_paths(GList *paths);
static void mark_menus_modified(gboolean mod);
static gboolean action_with_leaf(ActionFn action, guchar *current, guchar *new);

/* Note that for most of these callbacks none of the arguments are used. */
static void large(gpointer data, guint action, GtkWidget *widget);
static void small(gpointer data, guint action, GtkWidget *widget);

/* (action used in these two - (DetailsType) */
static void large_with(gpointer data, guint action, GtkWidget *widget);
static void small_with(gpointer data, guint action, GtkWidget *widget);

static void sort_name(gpointer data, guint action, GtkWidget *widget);
static void sort_type(gpointer data, guint action, GtkWidget *widget);
static void sort_size(gpointer data, guint action, GtkWidget *widget);
static void sort_date(gpointer data, guint action, GtkWidget *widget);

static void hidden(gpointer data, guint action, GtkWidget *widget);
static void refresh(gpointer data, guint action, GtkWidget *widget);

static void copy_item(gpointer data, guint action, GtkWidget *widget);
static void rename_item(gpointer data, guint action, GtkWidget *widget);
static void link_item(gpointer data, guint action, GtkWidget *widget);
static void open_file(gpointer data, guint action, GtkWidget *widget);
static void help(gpointer data, guint action, GtkWidget *widget);
static void show_file_info(gpointer data, guint action, GtkWidget *widget);
static void mount(gpointer data, guint action, GtkWidget *widget);
static void delete(gpointer data, guint action, GtkWidget *widget);
static void usage(gpointer data, guint action, GtkWidget *widget);
static void chmod_items(gpointer data, guint action, GtkWidget *widget);
static void find(gpointer data, guint action, GtkWidget *widget);

static void open_vfs_rpm(gpointer data, guint action, GtkWidget *widget);
static void open_vfs_utar(gpointer data, guint action, GtkWidget *widget);
static void open_vfs_uzip(gpointer data, guint action, GtkWidget *widget);

static void select_all(gpointer data, guint action, GtkWidget *widget);
static void clear_selection(gpointer data, guint action, GtkWidget *widget);
static void new_directory(gpointer data, guint action, GtkWidget *widget);
static void xterm_here(gpointer data, guint action, GtkWidget *widget);

static void open_parent_same(gpointer data, guint action, GtkWidget *widget);
static void open_parent(gpointer data, guint action, GtkWidget *widget);
static void new_window(gpointer data, guint action, GtkWidget *widget);
static void close_window(gpointer data, guint action, GtkWidget *widget);
static void enter_path(gpointer data, guint action, GtkWidget *widget);
static void shell_command(gpointer data, guint action, GtkWidget *widget);
static void run_action(gpointer data, guint action, GtkWidget *widget);
static void select_if(gpointer data, guint action, GtkWidget *widget);

static void pin_help(gpointer data, guint action, GtkWidget *widget);
static void pin_remove(gpointer data, guint action, GtkWidget *widget);

static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();

static OptionsSection options =
{
	N_("Menu options"),
	create_options,
	update_options,
	set_options,
	save_options
};


static GtkWidget	*filer_menu;		/* The popup filer menu */
static GtkWidget	*filer_file_item;	/* The File '' label */
static GtkWidget	*filer_file_menu;	/* The File '' menu */
static GtkWidget	*filer_vfs_menu;	/* The Open VFS menu */
static GtkWidget	*filer_hidden_menu;	/* The Show Hidden item */
static GtkWidget	*filer_new_window;	/* The New Window item */
static GtkWidget	*pinboard_menu;		/* The popup pinboard menu */

/* Used for Copy, etc */
static GtkWidget	*savebox = NULL;	
static guchar		*current_path = NULL;
static gboolean	(*current_savebox_callback)(guchar *current, guchar *new);

#undef N_
#define N_(x) x

static GtkItemFactoryEntry filer_menu_def[] = {
{N_("Display"),			NULL,	NULL, 0, "<Branch>"},
{">" N_("Large Icons"),   	NULL,  	large, 0, NULL},
{">" N_("Small Icons"),   	NULL,  	small, 0, NULL},
{">" N_("Large, With..."),	NULL,  	NULL, 0, "<Branch>"},
{">>" N_("Summary"),	NULL,  	large_with, DETAILS_SUMMARY, NULL},
{">>" N_("Sizes"),	NULL,  	large_with, DETAILS_SIZE, NULL},
{">>" N_("Size Bars"),	NULL,  	large_with, DETAILS_SIZE_BARS, NULL},
{">" N_("Small, With..."),	NULL,  	NULL, 0, "<Branch>"},
{">>" N_("Summary"),	NULL,  	small_with, DETAILS_SUMMARY, NULL},
{">>" N_("Sizes"),	NULL,  	small_with, DETAILS_SIZE, NULL},
{">>" N_("Size Bars"),	NULL,  	small_with, DETAILS_SIZE_BARS, NULL},
{">",			NULL,  	NULL, 0, "<Separator>"},
{">" N_("Sort by Name"),	NULL,  	sort_name, 0, NULL},
{">" N_("Sort by Type"),	NULL,  	sort_type, 0, NULL},
{">" N_("Sort by Date"),	NULL,  	sort_date, 0, NULL},
{">" N_("Sort by Size"),	NULL,  	sort_size, 0, NULL},
{">",			NULL,  	NULL, 0, "<Separator>"},
{">" N_("Show Hidden"),   	NULL, 	hidden, 0, "<ToggleItem>"},
{">" N_("Refresh"),	NULL, 	refresh, 0,	NULL},
{N_("File"),			NULL,  	NULL, 0, "<Branch>"},
{">" N_("Copy..."),	NULL,  	copy_item, 0, NULL},
{">" N_("Rename..."),	NULL,  	rename_item, 0, NULL},
{">" N_("Link..."),	NULL,  	link_item, 0, NULL},
{">" N_("Shift Open"),   	NULL,  	open_file, 0, NULL},
{">" N_("Help"),		NULL,  	help, 0, NULL},
{">" N_("Info"),		NULL,  	show_file_info, 0, NULL},
{">" N_("Open VFS"),	NULL,   NULL, 0, "<Branch>"},
{">>" N_("Unzip"),	NULL,   open_vfs_uzip, 0, NULL},
{">>" N_("Untar"),	NULL,   open_vfs_utar, 0, NULL},
{">>" N_("RPM"),		NULL,   open_vfs_rpm, 0, NULL},
{">",			NULL,   NULL, 0, "<Separator>"},
{">" N_("Mount"),	    	NULL,  	mount, 0,	NULL},
{">" N_("Delete"),	    	NULL, 	delete, 0,	NULL},
{">" N_("Disk Usage"),	NULL, 	usage, 0, NULL},
{">" N_("Permissions"),	NULL,   chmod_items, 0, NULL},
{">" N_("Find"),		NULL,   find, 0, NULL},
{N_("Select All"),	    	NULL,  	select_all, 0, NULL},
{N_("Clear Selection"),		NULL,  	clear_selection, 0, NULL},
{N_("Options..."),		NULL,   menu_show_options, 0, NULL},
{N_("New Directory..."),	NULL,   new_directory, 0, NULL},
{N_("Xterm Here"),		NULL,  	xterm_here, 0, NULL},
{N_("Window"),			NULL,  	NULL, 0, "<Branch>"},
{">" N_("Parent, New Window"), 	NULL,   open_parent, 0, NULL},
{">" N_("Parent, Same Window"), NULL,  open_parent_same, 0, NULL},
{">" N_("New Window"),	NULL,   new_window, 0, NULL},
{">" N_("Close Window"),	NULL,  	close_window, 0, NULL},
{">",			NULL,  	NULL, 0, "<Separator>"},
{">" N_("Enter Path..."),	NULL,  	enter_path, 0, NULL},
{">" N_("Shell Command..."),	NULL,  	shell_command, 0, NULL},
{">" N_("Set Run Action..."),	NULL,  	run_action, 0, NULL},
{">" N_("Select If..."),	NULL,  	select_if, 0, NULL},
{">",			NULL,  	NULL, 0, "<Separator>"},
{">" N_("Show ROX-Filer Help"), NULL,  menu_rox_help, 0, NULL},
};

static GtkItemFactoryEntry pinboard_menu_def[] = {
{N_("ROX-Filer Help"),		NULL,   menu_rox_help, 0, NULL},
{N_("ROX-Filer Options..."),	NULL,   menu_show_options, 0, NULL},
{"",				NULL,	NULL, 0, "<Separator>"},
{N_("Show Help"),    		NULL,  	pin_help, 0, NULL},
{N_("Remove Item(s)"),		NULL,	pin_remove, 0, NULL},
};


typedef struct _FileStatus FileStatus;

/* This is for the 'file(1) says...' thing */
struct _FileStatus
{
	int	fd;	/* FD to read from, -1 if closed */
	int	input;	/* Input watcher tag if fd valid */
	GtkLabel *label;	/* Widget to output to */
	gboolean	start;	/* No output yet */
};

#define GET_MENU_ITEM(var, menu)	\
		var = gtk_item_factory_get_widget(item_factory,	"<" menu ">");

#define GET_SMENU_ITEM(var, menu, sub)	\
	do {				\
		tmp = g_strdup_printf("<" menu ">/%s", _(sub));		\
		var = gtk_item_factory_get_widget(item_factory,	tmp); 	\
		g_free(tmp);		\
	} while (0)

#define GET_SSMENU_ITEM(var, menu, sub, subsub)	\
	do {				\
		tmp = g_strdup_printf("<" menu ">/%s/%s", _(sub), _(subsub)); \
		var = gtk_item_factory_get_widget(item_factory,	tmp); 	\
		g_free(tmp);		\
	} while (0)

/* Creates menu <name> from the <name>_menu_def array.
 * The accel group <name>_keys must also have been created.
 * All menu items are translated. Sets 'item_factory'.
 */
#define MAKE_MENU(name) \
do {									\
	GtkItemFactoryEntry	*translated;				\
	int			n_entries;				\
									\
	item_factory = gtk_item_factory_new(GTK_TYPE_MENU,		\
				"<" #name ">", name ## _keys);		\
									\
	n_entries = sizeof(name ## _menu_def) / sizeof(*name ## _menu_def); \
	translated = translate_entries(name ## _menu_def, n_entries);	\
	gtk_item_factory_create_items(item_factory, n_entries,		\
					translated, NULL);		\
	free_translated_entries(translated, n_entries);			\
} while (0)

void menu_init()
{
	char			*menurc;
	GList			*items;
	guchar			*tmp;
	GtkWidget		*item;
	GtkItemFactory  	*item_factory;

	filer_keys = gtk_accel_group_new();
	MAKE_MENU(filer);

	GET_MENU_ITEM(filer_menu, "filer");
	GET_SMENU_ITEM(filer_file_menu, "filer", "File");
	GET_SSMENU_ITEM(filer_vfs_menu, "filer", "File", "Open VFS");
	GET_SSMENU_ITEM(filer_hidden_menu, "filer", "Display", "Show Hidden");

	items = gtk_container_children(GTK_CONTAINER(filer_menu));
	filer_file_item = GTK_BIN(g_list_nth(items, 1)->data)->child;
	g_list_free(items);

	GET_SSMENU_ITEM(item, "filer", "Window", "New Window");
	filer_new_window = GTK_BIN(item)->child;

	menurc = choices_find_path_load("menus", PROJECT);
	if (menurc)
	{
		gtk_item_factory_parse_rc(menurc);
		mark_menus_modified(FALSE);
	}

	pinboard_keys = gtk_accel_group_new();
	MAKE_MENU(pinboard);
	gtk_accel_group_lock(pinboard_keys);
	GET_MENU_ITEM(pinboard_menu, "pinboard");

	gtk_signal_connect(GTK_OBJECT(pinboard_menu), "unmap_event",
			GTK_SIGNAL_FUNC(pin_menu_closed), NULL);
	gtk_signal_connect(GTK_OBJECT(filer_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);
	gtk_signal_connect(GTK_OBJECT(filer_file_menu), "unmap_event",
			GTK_SIGNAL_FUNC(menu_closed), NULL);

	options_sections = g_slist_prepend(options_sections, &options);
	xterm_here_value = g_strdup("xterm");
	option_register("xterm_here", load_xterm_here);

	savebox = gtk_savebox_new();
	gtk_signal_connect_object(GTK_OBJECT(savebox), "save_to_file",
				GTK_SIGNAL_FUNC(save_to_file), NULL);
	gtk_signal_connect_object(GTK_OBJECT(savebox), "save_done",
				GTK_SIGNAL_FUNC(gtk_widget_hide),
				GTK_OBJECT(savebox));

	atexit(save_menus);
}

/* Name is in the form "<panel>" */
GtkWidget *menu_create(GtkItemFactoryEntry *def, int n_entries, guchar *name)
{
	GtkItemFactory  	*item_factory;
	GtkItemFactoryEntry	*translated;
	GtkAccelGroup		*keys;
	GtkWidget		*menu;

	keys = gtk_accel_group_new();

	item_factory = gtk_item_factory_new(GTK_TYPE_MENU, name, keys);

	translated = translate_entries(def, n_entries);
	gtk_item_factory_create_items(item_factory, n_entries,
					translated, NULL);
	free_translated_entries(translated, n_entries);

	menu = gtk_item_factory_get_widget(item_factory, name);

	gtk_accel_group_lock(keys);

	gtk_signal_connect(GTK_OBJECT(menu), "unmap_event",
			GTK_SIGNAL_FUNC(pin_menu_closed), NULL);

	return menu;
}
 
/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options()
{
	GtkWidget	*table, *label;

	table = gtk_table_new(2, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 4);

	label = gtk_label_new(
			_("To set the keyboard short-cuts, simply open "
			"the menu over a filer window, move the pointer over "
			"the item you want to use and press a key. The key "
			"will appear next to the menu item and you can just "
			"press that key without opening the menu in future."));
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 2, 0, 1);

	label = gtk_label_new(_("'Xterm here' program:"));
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);
	xterm_here_entry = gtk_entry_new();
	gtk_table_attach_defaults(GTK_TABLE(table), xterm_here_entry,
			1, 2, 1, 2);

	return table;
}

static char *load_xterm_here(char *data)
{
	g_free(xterm_here_value);
	xterm_here_value = g_strdup(data);
	return NULL;
}

static void update_options()
{
	gtk_entry_set_text(GTK_ENTRY(xterm_here_entry), xterm_here_value);
}

static void set_options()
{
	g_free(xterm_here_value);
	xterm_here_value = g_strdup(gtk_entry_get_text(
				GTK_ENTRY(xterm_here_entry)));
}

static void save_options()
{
	save_menus();
	option_write("xterm_here", xterm_here_value);
}


static void items_sensitive(gboolean state)
{
	int	n = 7;
	GList	*items, *item;

	items = item = gtk_container_children(GTK_CONTAINER(filer_file_menu));
	while (item && n--)
	{
		gtk_widget_set_sensitive(GTK_BIN(item->data)->child, state);
		item = item->next;
	}
	g_list_free(items);

	items = item = gtk_container_children(GTK_CONTAINER(filer_vfs_menu));
	while (item)
	{
		gtk_widget_set_sensitive(GTK_BIN(item->data)->child, state);
		item = item->next;
	}
	g_list_free(items);
}

static void position_menu(GtkMenu *menu, gint *x, gint *y, gpointer data)
{
	int		*pos = (int *) data;
	GtkRequisition 	requisition;

	gtk_widget_size_request(GTK_WIDGET(menu), &requisition);

	*x = pos[0] - (requisition.width >> 2);
	*y = pos[1] - (requisition.height >> 2);

	*x = CLAMP(*x, 0, screen_width - requisition.width);
	*y = CLAMP(*y, 0, screen_height - requisition.height);
}

/* Display the pinboard menu. Set icon to NULL if no particular icon
 * was clicked.
 */
void show_pinboard_menu(GdkEventButton *event, PinIcon *icon)
{
	int		pos[2];
	GList		*icons;

	if (icon)
	{
		if (pinboard_is_selected(icon))
			pin_temp_item_selected = FALSE;
		else
		{
			pinboard_select_only(icon);
			pin_temp_item_selected = TRUE;
		}
	}

	icons = pinboard_get_selected();

	pos[0] = event->x_root;
	pos[1] = event->y_root;

	if (icons)
	{
		menu_set_items_shaded(pinboard_menu,
				icons->next ? TRUE : FALSE, 3, 1);

		menu_set_items_shaded(pinboard_menu, FALSE, 4, 1);
	}
	else
		menu_set_items_shaded(pinboard_menu, TRUE, 3, 2);

	gtk_menu_popup(GTK_MENU(pinboard_menu), NULL, NULL, position_menu,
			(gpointer) pos, event->button, event->time);
}

void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event,
		     int item)
{
	DirItem		*file_item;
	int		pos[2];

	updating_menu++;
	
	pos[0] = event->x_root;
	pos[1] = event->y_root;

	window_with_focus = filer_window;

	if (filer_window->collection->number_selected == 0 && item >= 0)
	{
		collection_select_item(filer_window->collection, item);
		filer_window->temp_item_selected = TRUE;
	}
	else
		filer_window->temp_item_selected = FALSE;

	{
		GtkWidget	*file_label, *file_menu;
		Collection 	*collection = filer_window->collection;
		GString		*buffer;

		file_label = filer_file_item;
		file_menu = filer_file_menu;
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_hidden_menu),
				filer_window->show_hidden);
		buffer = g_string_new(NULL);
		switch (collection->number_selected)
		{
			case 0:
				g_string_assign(buffer, _("Next Click"));
				items_sensitive(TRUE);
				break;
			case 1:
				items_sensitive(TRUE);
				file_item = selected_item(
						filer_window->collection);
				g_string_sprintf(buffer, "%s '%s'",
						basetype_name(file_item),
						file_item->leafname);
				break;
			default:
				items_sensitive(FALSE);
				g_string_sprintf(buffer, _("%d items"),
						collection->number_selected);
				break;
		}
		gtk_label_set_text(GTK_LABEL(file_label), buffer->str);
		g_string_free(buffer, TRUE);
	}


	gtk_widget_set_sensitive(filer_new_window, !o_unique_filer_windows);

	popup_menu = (event->state & GDK_CONTROL_MASK)
				? filer_file_menu
				: filer_menu;

	updating_menu--;
	
	gtk_menu_popup(GTK_MENU(popup_menu), NULL, NULL, position_menu,
			(gpointer) pos, event->button, event->time);
}

static void pin_menu_closed(GtkWidget *widget)
{
	if (pin_temp_item_selected)
		pinboard_clear_selection();
}

static void menu_closed(GtkWidget *widget)
{
	if (window_with_focus == NULL || widget != popup_menu)
		return;			/* Close panel item chosen? */

	popup_menu = NULL;

	if (window_with_focus->temp_item_selected)
	{
		collection_clear_selection(window_with_focus->collection);
		window_with_focus->temp_item_selected = FALSE;
	}
}

void target_callback(FilerWindow *filer_window,
			gint item,
			gpointer real_fn)
{
	Collection	*collection = filer_window->collection;

	g_return_if_fail(window_with_focus != NULL);
	g_return_if_fail(window_with_focus == filer_window);
	g_return_if_fail(real_fn != NULL);
	
	collection_wink_item(collection, item);
	collection_clear_selection(collection);
	collection_select_item(collection, item);
	((GtkItemFactoryCallback1) real_fn)(NULL, 0, GTK_WIDGET(collection));
	if (item < collection->number_of_items)
		collection_unselect_item(collection, item);
}

/* Actions */

static void large(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_set_layout(window_with_focus, "Large");
}

static void small(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_set_layout(window_with_focus, "Small");
}

static void set_layout(gboolean large, DetailsType details)
{
	guchar	*style;

	g_return_if_fail(window_with_focus != NULL);

	style = g_strdup_printf("%s+%s",
		large ? "Large" : "Small",
		details == DETAILS_SUMMARY ? "Summary" :
		details == DETAILS_SIZE_BARS ? "SizeBars" :
		"Sizes");

	display_set_layout(window_with_focus, style);
	g_free(style);
}

static void large_with(gpointer data, guint action, GtkWidget *widget)
{
	set_layout(TRUE, (DetailsType) action);
}

static void small_with(gpointer data, guint action, GtkWidget *widget)
{
	set_layout(FALSE, (DetailsType) action);
}

static void sort_name(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_fn(window_with_focus, sort_by_name);
}

static void sort_type(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_fn(window_with_focus, sort_by_type);
}

static void sort_date(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_fn(window_with_focus, sort_by_date);
}

static void sort_size(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_fn(window_with_focus, sort_by_size);
}

static void hidden(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_hidden(window_with_focus, !window_with_focus->show_hidden);
}

static void refresh(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	full_refresh();
	filer_update_dir(window_with_focus, TRUE);
}

static void mount(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (window_with_focus->collection->number_selected == 0)
		filer_target_mode(window_with_focus,
				target_callback, mount,
				_("Mount ... ?"));
	else
	{
		GList	*paths;

		paths = list_paths(window_with_focus);
		action_mount(paths);
		free_paths(paths);
	}
}

static void delete(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (window_with_focus->collection->number_selected == 0)
		filer_target_mode(window_with_focus,
				target_callback, delete,
				_("DELETE ... ?"));
	else
		action_delete(window_with_focus);
}

static void usage(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (window_with_focus->collection->number_selected == 0)
		filer_target_mode(window_with_focus,
				target_callback, usage,
				_("Count the size of ... ?"));
	else
		action_usage(window_with_focus);
}

static void chmod_items(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (window_with_focus->collection->number_selected == 0)
		filer_target_mode(window_with_focus,
				target_callback, chmod_items,
				_("Set permissions on ... ?"));
	else
		action_chmod(window_with_focus);
}

static void find(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (window_with_focus->collection->number_selected == 0)
		filer_target_mode(window_with_focus,
				target_callback, find,
				_("Search inside ... ?"));
	else
		action_find(window_with_focus);
}

/* This pops up our savebox widget, cancelling any currently open one,
 * and allows the user to pick a new path for it.
 * Once the new path has been picked, the callback will be called with
 * both the current and new paths.
 */
static void savebox_show(guchar *title, guchar *path, MaskedPixmap *image,
		gboolean (*callback)(guchar *current, guchar *new))
{
	if (GTK_WIDGET_VISIBLE(savebox))
		gtk_widget_hide(savebox);

	if (current_path)
		g_free(current_path);
	current_path = g_strdup(path);
	current_savebox_callback = callback;

	gtk_window_set_title(GTK_WINDOW(savebox), title);
	gtk_savebox_set_pathname(GTK_SAVEBOX(savebox), current_path);
	gtk_savebox_set_icon(GTK_SAVEBOX(savebox), image->pixmap, image->mask);
				
	gtk_widget_grab_focus(GTK_SAVEBOX(savebox)->entry);
	gtk_widget_show(savebox);
}

static gint save_to_file(GtkSavebox *savebox, guchar *pathname)
{
	g_return_val_if_fail(current_savebox_callback != NULL,
			GTK_XDS_SAVE_ERROR);

	return current_savebox_callback(current_path, pathname)
			? GTK_XDS_SAVED : GTK_XDS_SAVE_ERROR;
}

static gboolean copy_cb(guchar *current, guchar *new)
{
	return action_with_leaf(action_copy, current, new);
}

static gboolean action_with_leaf(ActionFn action, guchar *current, guchar *new)
{
	char	*new_dir, *leaf;
	GSList	*local_paths;

	if (new[0] != '/')
	{
		report_error(PROJECT, _("New pathname is not absolute"));
		return FALSE;
	}

	if (new[strlen(new) - 1] == '/')
	{
		new_dir = g_strdup(new);
		leaf = NULL;
	}
	else
	{
		guchar *slash;
		
		slash = strrchr(new, '/');
		new_dir = g_strndup(new, slash - new);
		leaf = slash + 1;
	}

	local_paths = g_slist_append(NULL, current);
	action(local_paths, new_dir, leaf);
	g_slist_free(local_paths);

	g_free(new_dir);

	return TRUE;
}

#define SHOW_SAVEBOX(title, callback)	\
{	\
	DirItem	*item;	\
	guchar	*path;	\
	item = selected_item(collection);	\
	path = make_path(window_with_focus->path, item->leafname)->str;	\
	savebox_show(title, path, item->image, callback);	\
}

static void copy_item(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error(PROJECT, _("You cannot do this to more than "
						"one item at a time"));
		return;
	}
	else if (collection->number_selected != 1)
		filer_target_mode(window_with_focus,
				target_callback, copy_item,
				_("Copy ... ?"));
	else
		SHOW_SAVEBOX(_("Copy"), copy_cb);
}

static gboolean rename_cb(guchar *current, guchar *new)
{
	return action_with_leaf(action_move, current, new);
}

static void rename_item(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error(PROJECT, _("You cannot do this to more than "
				"one item at a time"));
		return;
	}
	else if (collection->number_selected != 1)
		filer_target_mode(window_with_focus,
				target_callback, rename_item,
				_("Rename ... ?"));
	else
		SHOW_SAVEBOX(_("Rename"), rename_cb);
}

static gboolean link_cb(guchar *initial, guchar *path)
{
	if (symlink(initial, path))
	{
		report_error("ROX-Filer: symlink()", g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static void link_item(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error(PROJECT, _("You cannot do this to more than "
				"one item at a time"));
		return;
	}
	else if (collection->number_selected != 1)
		filer_target_mode(window_with_focus,
				target_callback, link_item,
				_("Symlink ... ?"));
	else
		SHOW_SAVEBOX(_("Symlink"), link_cb);
}

static void open_file(gpointer data, guint action, GtkWidget *widget)
{
	Collection *collection;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error(PROJECT, _("You cannot do this to more than "
				"one item at a time"));
		return;
	}
	else if (collection->number_selected != 1)
		filer_target_mode(window_with_focus,
				target_callback, open_file,
				_("Shift open ... ?"));
	else
		filer_openitem(window_with_focus,
				selected_item_number(collection),
				OPEN_SAME_WINDOW | OPEN_SHIFT);
}

/* Got some data from file(1) - stick it in the window. */
static void add_file_output(FileStatus *fs,
			    gint source, GdkInputCondition condition)
{
	char	buffer[20];
	char	*str;
	int	got;

	got = read(source, buffer, sizeof(buffer) - 1);
	if (got <= 0)
	{
		int	err = errno;
		gtk_input_remove(fs->input);
		close(source);
		fs->fd = -1;
		if (got < 0)
			delayed_error(_("ROX-Filer: file(1) says..."),
					g_strerror(err));
		return;
	}
	buffer[got] = '\0';

	if (fs->start)
	{
		str = "";
		fs->start = FALSE;
	}
	else
		gtk_label_get(fs->label, &str);

	str = g_strconcat(str, buffer, NULL);
	gtk_label_set_text(fs->label, str);
	g_free(str);
}

static void file_info_destroyed(GtkWidget *widget, FileStatus *fs)
{
	if (fs->fd != -1)
	{
		gtk_input_remove(fs->input);
		close(fs->fd);
	}

	g_free(fs);
}

/* g_free() the result */
guchar *pretty_type(DirItem *file, guchar *path)
{
	if (file->flags & ITEM_FLAG_SYMLINK)
	{
		char	p[MAXPATHLEN + 1];
		int	got;
		got = readlink(path, p, MAXPATHLEN);
		if (got > 0 && got <= MAXPATHLEN)
		{
			p[got] = '\0';
			return g_strconcat(_("Symbolic link to "), p, NULL);
		}

		return g_strdup(_("Symbolic link"));
	}

	if (file->flags & ITEM_FLAG_EXEC_FILE)
		return g_strdup(_("Executable file"));

	if (file->flags & ITEM_FLAG_APPDIR)
		return g_strdup(_("ROX application"));

	if (file->flags & ITEM_FLAG_MOUNT_POINT)
	{
		MountPoint *mp;
		if ((file->flags & ITEM_FLAG_MOUNTED) &&
			(mp = g_hash_table_lookup(mtab_mounts, path)))
			return g_strconcat(_("Mount point for "),
					mp->name, NULL);

		return g_strdup(_("Mount point"));
	}

	if (file->mime_type)
		return g_strconcat(file->mime_type->media_type, "/",
				file->mime_type->subtype, NULL);

	return g_strdup("-");
}

#define LABEL(text, row)						\
	label = gtk_label_new(text);				\
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);			\
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_RIGHT);	\
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, row, row + 1);

#define VALUE(label, row)						\
	gtk_misc_set_alignment(GTK_MISC(label), 0, .5);			\
	gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, row, row + 1);

static void show_file_info(gpointer data, guint action, GtkWidget *widget)
{
	GtkWidget	*window, *table, *label, *button, *frame, *value;
	GtkWidget	*file_label;
	GString		*gstring;
	int		file_data[2];
	guchar		*path, *tmp;
	char 		*argv[] = {"file", "-b", NULL, NULL};
	Collection 	*collection;
	DirItem		*file;
	struct stat	info;
	FileStatus 	*fs = NULL;
	guint		perm;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error(PROJECT, _("You cannot do this to more than "
				"one item at a time"));
		return;
	}
	else if (collection->number_selected != 1)
	{
		filer_target_mode(window_with_focus,
				target_callback, show_file_info,
				_("Examine ... ?"));
		return;
	}
	file = selected_item(collection);
	path = make_path(window_with_focus->path,
			file->leafname)->str;
	if (lstat(path, &info))
	{
		delayed_error(PROJECT, g_strerror(errno));
		return;
	}
	
	gstring = g_string_new(NULL);

	window = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_MOUSE);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);
	gtk_window_set_title(GTK_WINDOW(window), path);

	table = gtk_table_new(10, 2, FALSE);
	gtk_container_add(GTK_CONTAINER(window), table);
	gtk_table_set_row_spacings(GTK_TABLE(table), 8);
	gtk_table_set_col_spacings(GTK_TABLE(table), 4);
	
	value = gtk_label_new(file->leafname);
	LABEL(_("Name:"), 0);
	VALUE(value, 0);

	g_string_sprintf(gstring, "%s, %s", user_name(info.st_uid),
					    group_name(info.st_gid));
	value = gtk_label_new(gstring->str);
	LABEL(_("Owner, Group:"), 1);
	VALUE(value, 1);
	
	if (info.st_size >= PRETTY_SIZE_LIMIT)
	{
		g_string_sprintf(gstring, "%s (%ld %s)",
				format_size((unsigned long) info.st_size),
				(unsigned long) info.st_size, _("bytes"));
	}
	else
	{
		g_string_assign(gstring, 
				format_size((unsigned long) info.st_size));
	}
	value = gtk_label_new(gstring->str);
	LABEL(_("Size:"), 2);
	VALUE(value, 2);
	
	value = gtk_label_new(pretty_time(&info.st_ctime));
	LABEL(_("Change time:"), 3);
	VALUE(value, 3);
	
	value = gtk_label_new(pretty_time(&info.st_mtime));
	LABEL(_("Modify time:"), 4);
	VALUE(value, 4);
	
	value = gtk_label_new(pretty_time(&info.st_atime));
	LABEL(_("Access time:"), 5);
	VALUE(value, 5);
	
	value = gtk_label_new(pretty_permissions(info.st_mode));
	perm = applicable(info.st_uid, info.st_gid);
	gtk_label_set_pattern(GTK_LABEL(value),
			perm == 0 ? "___        " :
			perm == 1 ? "    ___    " :
				    "        ___");
	gtk_widget_set_style(value, fixed_style);
	LABEL(_("Permissions:"), 6);
	VALUE(value, 6);
	
	tmp = pretty_type(file, path);
	value = gtk_label_new(tmp);
	g_free(tmp);
	LABEL(_("Type:"), 7);
	VALUE(value, 7);

	frame = gtk_frame_new(_("file(1) says..."));
	gtk_table_attach_defaults(GTK_TABLE(table), frame, 0, 2, 8, 9);
	file_label = gtk_label_new(_("<nothing yet>"));
	gtk_misc_set_padding(GTK_MISC(file_label), 4, 4);
	gtk_label_set_line_wrap(GTK_LABEL(file_label), TRUE);
	gtk_container_add(GTK_CONTAINER(frame), file_label);
	
	button = gtk_button_new_with_label(_("OK"));
	gtk_window_set_focus(GTK_WINDOW(window), button);
	gtk_table_attach(GTK_TABLE(table), button, 0, 2, 10, 11,
			GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0, 40, 4);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			gtk_widget_destroy, GTK_OBJECT(window));

	gtk_widget_show_all(window);
	gdk_flush();

	if (pipe(file_data))
	{
		g_string_sprintf(gstring, "pipe(): %s", g_strerror(errno));
		g_string_free(gstring, TRUE);
		return;
	}
	switch (fork())
	{
		case -1:
			g_string_sprintf(gstring, "fork(): %s",
					g_strerror(errno));
			gtk_label_set_text(GTK_LABEL(file_label), gstring->str);
			g_string_free(gstring, TRUE);
			close(file_data[0]);
			close(file_data[1]);
			break;
		case 0:
			/* We are the child */
			close(file_data[0]);
			dup2(file_data[1], STDOUT_FILENO);
			dup2(file_data[1], STDERR_FILENO);
#ifdef FILE_B_FLAG
			argv[2] = path;
#else
			argv[1] = file->leafname;
			chdir(window_with_focus->path);
#endif
			if (execvp(argv[0], argv))
				fprintf(stderr, "execvp() error: %s\n",
						g_strerror(errno));
			_exit(0);
		default:
			/* We are the parent */
			close(file_data[1]);
			fs = g_new(FileStatus, 1);
			fs->label = GTK_LABEL(file_label);
			fs->fd = file_data[0];
			fs->start = TRUE;
			fs->input = gdk_input_add(fs->fd, GDK_INPUT_READ,
				(GdkInputFunction) add_file_output, fs);
			gtk_signal_connect(GTK_OBJECT(window), "destroy",
				GTK_SIGNAL_FUNC(file_info_destroyed), fs);
			g_string_free(gstring, TRUE);
			break;
	}
}

static void help(gpointer data, guint action, GtkWidget *widget)
{
	Collection 	*collection;
	DirItem	*item;
	
	g_return_if_fail(window_with_focus != NULL);

	collection = window_with_focus->collection;
	if (collection->number_selected > 1)
	{
		report_error(PROJECT, _("You cannot do this to more than "
				"one item at a time"));
		return;
	}
	else if (collection->number_selected != 1)
	{
		filer_target_mode(window_with_focus, target_callback, help,
				_("Help about ... ?"));
		return;
	}
	item = selected_item(collection);

	show_item_help(make_path(window_with_focus->path, item->leafname)->str,
			item);
}

#define OPEN_VFS(fs)		\
static void open_vfs_ ## fs (gpointer data, guint action, GtkWidget *widget) \
{							\
	Collection 	*collection;			\
							\
	g_return_if_fail(window_with_focus != NULL);	\
							\
	collection = window_with_focus->collection;	\
	if (collection->number_selected < 1)			\
		filer_target_mode(window_with_focus, target_callback,	\
					open_vfs_ ## fs,		\
					_("Look inside ... ?"));	\
	else								\
		real_vfs_open(#fs);			\
}

static void real_vfs_open(char *fs)
{
	gchar		*path;
	DirItem		*item;

	if (window_with_focus->collection->number_selected != 1)
	{
		report_error(PROJECT, _("You must select a single file "
				"to open as a Virtual File System"));
		return;
	}

	item = selected_item(window_with_focus->collection);

	path = g_strconcat(window_with_focus->path,
			"/",
			item->leafname,
			"#", fs, NULL);

	filer_change_to(window_with_focus, path, NULL);
	g_free(path);
}

OPEN_VFS(rpm)
OPEN_VFS(utar)
OPEN_VFS(uzip)

static void select_all(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	collection_select_all(window_with_focus->collection);
	window_with_focus->temp_item_selected = FALSE;
}

static void clear_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	collection_clear_selection(window_with_focus->collection);
	window_with_focus->temp_item_selected = FALSE;
}

void menu_show_options(gpointer data, guint action, GtkWidget *widget)
{
	options_show();
}

static gboolean new_directory_cb(guchar *initial, guchar *path)
{
	if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO))
	{
		report_error("mkdir", g_strerror(errno));
		return FALSE;
	}

	dir_check_this(path);
	return TRUE;
}

static void new_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	savebox_show(_("New Directory"),
			make_path(window_with_focus->path, _("NewDir"))->str,
			type_to_icon(&special_directory),
			new_directory_cb);
}

static void xterm_here(gpointer data, guint action, GtkWidget *widget)
{
	char	*argv[] = {NULL, NULL};

	argv[0] = xterm_here_value;

	g_return_if_fail(window_with_focus != NULL);

	if (!spawn_full(argv, window_with_focus->path))
		report_error(PROJECT, _("Failed to fork() child process"));
}

static void open_parent(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_open_parent(window_with_focus);
}

static void open_parent_same(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	change_to_parent(window_with_focus);
}

static void new_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (o_unique_filer_windows)
		report_error(PROJECT, _("You can't open a second view onto "
			"this directory because the `Unique Windows' option "
			"is turned on in the Options window."));
	else
		filer_opendir(window_with_focus->path);
}

static void close_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	gtk_widget_destroy(window_with_focus->window);
}

static void enter_path(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	minibuffer_show(window_with_focus, MINI_PATH);
}

static void shell_command(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	minibuffer_show(window_with_focus, MINI_SHELL);
}

static void run_action(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	minibuffer_show(window_with_focus, MINI_RUN_ACTION);
}

static void select_if(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	minibuffer_show(window_with_focus, MINI_SELECT_IF);
}

void menu_rox_help(gpointer data, guint action, GtkWidget *widget)
{
	filer_opendir(make_path(app_dir, "Help")->str);
}

/* Return a list of full paths of all the selected items */
static GList *list_paths(FilerWindow *filer_window)
{
	Collection	*collection = filer_window->collection;
	GList		*paths = NULL;
	int		i;
	
	for (i = 0; i < collection->number_of_items; i++)
	{
		CollectionItem	*colitem = &collection->items[i];
		
		if (colitem->selected)
		{
			DirItem	*item = (DirItem *) colitem->data;

			paths = g_list_prepend(paths,
				g_strdup(make_path(filer_window->path,
						   item->leafname)->str));
		}
	}

	return paths;
}

static void free_paths(GList *paths)
{
	GList	*next;

	if (!paths)
		return;

	for (next = paths; next; next = next->next)
		g_free(next->data);

	g_list_free(paths);
}

static void pin_help(gpointer data, guint action, GtkWidget *widget)
{
	PinIcon	*icon;

	icon = pinboard_selected_icon();

	if (icon)
		pinboard_show_help(icon);
	else
		delayed_error(PROJECT,
			_("You must first select a single pinned icon to get "
			"help on."));
}

static void pin_remove(gpointer data, guint action, GtkWidget *widget)
{
	pinboard_unpin_selection();
}

/* Set n items from position 'from' in 'menu' to the 'shaded' state */
void menu_set_items_shaded(GtkWidget *menu, gboolean shaded, int from, int n)
{
	GList	*items, *item;

	items = gtk_container_children(GTK_CONTAINER(menu));

	item = g_list_nth(items, from);
	while (item && n--)
	{
		gtk_widget_set_sensitive(GTK_BIN(item->data)->child, !shaded);
		item = item->next;
	}
	g_list_free(items);
}

/* This is called for every modified menu entry. We just use it to
 * find out if the menu has changed at all.
 */
static void set_mod(gboolean *mod, guchar *str)
{
	if (str && str[0] == '(')
		*mod = TRUE;
}

static void save_menus(void)
{
	char	*menurc;
	
	menurc = choices_find_path_save("menus", PROJECT, TRUE);
	if (menurc)
	{
		gboolean	mod = FALSE;

		/* Find out if anything changed... */
		gtk_item_factory_dump_items(NULL, TRUE,
				(GtkPrintFunc) set_mod, &mod);

		/* Dump out if so... */
		if (mod)
		{
			mark_menus_modified(TRUE);
			gtk_item_factory_dump_rc(menurc, NULL, TRUE);
			mark_menus_modified(FALSE);
		}
	}
}

static void mark_modified(gpointer hash_key,
			  gpointer value,
			  gpointer user_data)
{
	GtkItemFactoryItem *item = (GtkItemFactoryItem *) value;

	item->modified = (gboolean) user_data;
}

/* Set or clear the 'modified' flag in all menu items. Messy... */
static void mark_menus_modified(gboolean mod)
{
	GtkItemFactoryClass	*class;

	class = gtk_type_class(GTK_TYPE_ITEM_FACTORY);

	g_hash_table_foreach(class->item_ht, mark_modified, (gpointer) mod);
}
