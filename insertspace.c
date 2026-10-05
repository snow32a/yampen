#include <gtk/gtk.h>
#include "glibconfig.h"
#include "globals.h"
#include "gtk/gtkshortcut.h"
#include "protocol/yamp.h"
typedef struct {
	GtkWidget* stack;
	GtkWidget* datapage;
	GtkWidget* navbar;
	GtkWidget* back_btn;
	GtkWidget* next_btn;

	GtkWidget* nameentry;
	GtkWidget* display_nameentry;
	GtkWidget* descview;

	gboolean is_community;
} SpaceWizard;

static const char* PAGE_ORDER[] = {"type", "data", "confirm"};
#define PAGE_COUNT 3

static int page_index(const char* name) {
	for (int i = 0; i < PAGE_COUNT; i++)
		if (g_strcmp0(name, PAGE_ORDER[i]) == 0)
			return i;
	return 0;
}

void rebuild_datapage(SpaceWizard* wiz) {
	GtkWidget* child;
	while ((child = gtk_widget_get_first_child(wiz->datapage)) != NULL)
		gtk_box_remove(GTK_BOX(wiz->datapage), child);

	GtkWidget* dattitle = gtk_label_new(NULL);
	gtk_widget_set_size_request(dattitle, -1, 60);
	gtk_label_set_xalign(GTK_LABEL(dattitle), 0.5f);
	gtk_label_set_yalign(GTK_LABEL(dattitle), 0.5f);
	gtk_label_set_markup(
		GTK_LABEL(dattitle),
		"<span size=\"large\" weight=\"bold\">Configure your space</span>");
	gtk_box_append(GTK_BOX(wiz->datapage), dattitle);

	const int icon_size = 64;

	if (wiz->is_community) {
		GtkWidget* mediaovl = gtk_overlay_new();
		gtk_widget_set_size_request(mediaovl, -1,
									icon_size + 10 + icon_size / 2);

		GtkWidget* banner = gtk_frame_new(NULL);
		gtk_widget_set_size_request(banner, -1, icon_size + 10);
		gtk_widget_set_valign(banner, GTK_ALIGN_START);
		gtk_overlay_set_child(GTK_OVERLAY(mediaovl), banner);

		GtkWidget* icon = gtk_frame_new(NULL);
		gtk_widget_set_size_request(icon, icon_size, icon_size);
		gtk_widget_set_halign(icon, GTK_ALIGN_START);
		gtk_widget_set_valign(icon, GTK_ALIGN_START);
		gtk_widget_set_margin_start(icon, 12);

		gtk_widget_set_margin_top(icon, icon_size + 10 - icon_size / 2);
		gtk_overlay_add_overlay(GTK_OVERLAY(mediaovl), icon);

		gtk_box_append(GTK_BOX(wiz->datapage), mediaovl);
	} else {
		GtkWidget* icon = gtk_frame_new(NULL);
		gtk_widget_set_size_request(icon, icon_size, icon_size);
		gtk_widget_set_halign(icon, GTK_ALIGN_START);
		gtk_widget_set_margin_start(icon, 12);
		gtk_widget_set_margin_top(icon, 8);
		gtk_box_append(GTK_BOX(wiz->datapage), icon);
	}

	GtkWidget* dpynamelabel = gtk_label_new("Display Name");
	gtk_label_set_xalign(GTK_LABEL(dpynamelabel), 0);
	GtkWidget* dpynamedesc =
		gtk_label_new("Used as the visible name of the space");
	gtk_label_set_xalign(GTK_LABEL(dpynamedesc), 0);
	gtk_widget_set_opacity(dpynamedesc, 0.6f);
	GtkWidget* dpynameentry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(dpynameentry), "My Community");
	wiz->display_nameentry = dpynameentry;

	gtk_box_append(GTK_BOX(wiz->datapage), dpynamelabel);
	gtk_box_append(GTK_BOX(wiz->datapage), dpynamedesc);
	gtk_box_append(GTK_BOX(wiz->datapage), dpynameentry);

	GtkWidget* namelabel = gtk_label_new("Name");
	gtk_label_set_xalign(GTK_LABEL(namelabel), 0);
	GtkWidget* namedesc = gtk_label_new("Used as an ID for the space");
	gtk_label_set_xalign(GTK_LABEL(namedesc), 0);
	gtk_widget_set_opacity(namedesc, 0.6f);
	GtkWidget* nameentry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(nameentry), "my-community");
	wiz->nameentry = nameentry;

	gtk_box_append(GTK_BOX(wiz->datapage), namelabel);
	gtk_box_append(GTK_BOX(wiz->datapage), namedesc);
	gtk_box_append(GTK_BOX(wiz->datapage), nameentry);

	if (wiz->is_community) {
		GtkWidget* desclabel = gtk_label_new("Description");
		gtk_label_set_xalign(GTK_LABEL(desclabel), 0);
		GtkWidget* descdesc =
			gtk_label_new("Shown to visitors on the space page");
		gtk_label_set_xalign(GTK_LABEL(descdesc), 0);
		gtk_widget_set_opacity(descdesc, 0.6f);

		GtkWidget* descview = gtk_text_view_new();
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(descview),
									GTK_WRAP_WORD_CHAR);
		gtk_widget_set_size_request(descview, -1, 80);
		wiz->descview = descview;

		GtkWidget* descscroll = gtk_scrolled_window_new();
		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(descscroll),
									  descview);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(descscroll),
									   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		gtk_box_append(GTK_BOX(wiz->datapage), desclabel);
		gtk_box_append(GTK_BOX(wiz->datapage), descdesc);
		gtk_box_append(GTK_BOX(wiz->datapage), descscroll);
	}
}

void update_navbar(SpaceWizard* wiz) {
	const char* current =
		gtk_stack_get_visible_child_name(GTK_STACK(wiz->stack));
	int idx = page_index(current);

	gtk_widget_set_visible(wiz->back_btn, idx > 0);
	gtk_widget_set_visible(wiz->next_btn, idx < PAGE_COUNT - 1 && idx != 0);
	gtk_button_set_label(GTK_BUTTON(wiz->next_btn), "Next");
}

void cb_go_back(GtkButton* btn, gpointer dat) {
	SpaceWizard* wiz = dat;
	const char* current =
		gtk_stack_get_visible_child_name(GTK_STACK(wiz->stack));
	int idx = page_index(current);
	if (idx > 0)
		gtk_stack_set_visible_child_name(GTK_STACK(wiz->stack),
										 PAGE_ORDER[idx - 1]);
	update_navbar(wiz);
}

void cb_go_next(GtkButton* btn, gpointer dat) {
	SpaceWizard* wiz = dat;
	const char* current =
		gtk_stack_get_visible_child_name(GTK_STACK(wiz->stack));
	int idx = page_index(current);

	gtk_stack_set_visible_child_name(GTK_STACK(wiz->stack),
									 PAGE_ORDER[idx + 1]);
	update_navbar(wiz);
}
void cb_finish_space_insert(GtkButton* btn, gpointer dat) {
	SpaceWizard* wiz = dat;

	const char* name = gtk_editable_get_text(GTK_EDITABLE(wiz->nameentry));

	const char* display_name =
		gtk_editable_get_text(GTK_EDITABLE(wiz->display_nameentry));

	char* name_arg = NULL;
	char* display_name_arg = NULL;
	char* description_arg = NULL;

	if (name && *name)
		name_arg = g_strdup(name);

	if (display_name && *display_name)
		display_name_arg = g_strdup(display_name);

	if (wiz->is_community && wiz->descview) {
		GtkTextBuffer* buffer =
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(wiz->descview));

		GtkTextIter start, end;
		gtk_text_buffer_get_bounds(buffer, &start, &end);

		description_arg = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

		if (!description_arg || !*description_arg) {
			g_free(description_arg);
			description_arg = NULL;
		}
	}

	YAMPInsertSpace(mainsock, name_arg, display_name_arg,
					wiz->is_community, description_arg,
					NULL, /* banner */
					NULL  /* pfp */
	);

	g_free(name_arg);
	g_free(display_name_arg);
	g_free(description_arg);
}
void spacechoice(GtkListBox* box, GtkListBoxRow* row, gpointer dat) {
	SpaceWizard* wiz = dat;
	GtkWidget* child = gtk_list_box_row_get_child(row);
	const char* text = gtk_label_get_text(GTK_LABEL(child));

	wiz->is_community = (g_strrstr(text, "Community") != NULL);
	rebuild_datapage(wiz);

	gtk_stack_set_visible_child_name(GTK_STACK(wiz->stack), "data");
	update_navbar(wiz);
}

void cb_insert_space(GtkButton* btn, gpointer dat) {
	GtkWidget* gwnd = gtk_application_window_new(global_app);

	gtk_window_set_default_size(GTK_WINDOW(gwnd), 300, 400);
	gtk_window_set_resizable(GTK_WINDOW(gwnd), FALSE);
	gtk_window_set_decorated(GTK_WINDOW(gwnd), TRUE);

	SpaceWizard* wiz = g_new0(SpaceWizard, 1);

	GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	GtkWidget* stack = gtk_stack_new();
	gtk_widget_set_vexpand(stack, TRUE);
	wiz->stack = stack;

	GtkWidget* typepage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	GtkWidget* title = gtk_label_new(NULL);
	gtk_widget_set_size_request(title, -1, 60);
	gtk_label_set_xalign(GTK_LABEL(title), 0.5f);
	gtk_label_set_yalign(GTK_LABEL(title), 0.5f);
	gtk_label_set_markup(
		GTK_LABEL(title),
		"<span size=\"large\" weight=\"bold\">Create a space</span>");

	GtkWidget* optionlist = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(optionlist),
									GTK_SELECTION_NONE);

	GtkWidget* privatespace = gtk_list_box_row_new();
	gtk_widget_set_size_request(privatespace, -1, 80);
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(privatespace),
							   gtk_label_new("› Private/Friend Space"));

	GtkWidget* communityspace = gtk_list_box_row_new();
	gtk_widget_set_size_request(communityspace, -1, 80);
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(communityspace),
							   gtk_label_new("› Community Space"));

	g_signal_connect(optionlist, "row-activated", G_CALLBACK(spacechoice), wiz);

	gtk_list_box_append(GTK_LIST_BOX(optionlist), privatespace);
	gtk_list_box_append(GTK_LIST_BOX(optionlist), communityspace);

	gtk_box_append(GTK_BOX(typepage), title);
	gtk_box_append(GTK_BOX(typepage), optionlist);

	GtkWidget* datapage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_margin_start(datapage, 12);
	gtk_widget_set_margin_end(datapage, 12);

	wiz->datapage = datapage;

	GtkWidget* confirmpage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

	GtkWidget* confirmtitle = gtk_label_new(NULL);
	gtk_widget_set_size_request(confirmtitle, -1, 60);
	gtk_label_set_xalign(GTK_LABEL(confirmtitle), 0.5f);
	gtk_label_set_yalign(GTK_LABEL(confirmtitle), 0.5f);
	gtk_label_set_markup(GTK_LABEL(confirmtitle),
						 "<span size=\"large\" weight=\"bold\">Do you want to "
						 "insert this space</span>");
	gtk_box_append(GTK_BOX(confirmpage), confirmtitle);
	gtk_widget_set_margin_start(confirmpage, 12);
	gtk_widget_set_margin_end(confirmpage, 12);
	GtkWidget* insertbtn = gtk_button_new_with_label("Insert!");
	g_signal_connect(insertbtn, "clicked", G_CALLBACK(cb_finish_space_insert),
					 wiz);
	gtk_widget_set_hexpand(insertbtn, TRUE);
	gtk_box_append(GTK_BOX(confirmpage), insertbtn);

	gtk_stack_add_named(GTK_STACK(stack), typepage, "type");
	gtk_stack_add_named(GTK_STACK(stack), datapage, "data");
	gtk_stack_add_named(GTK_STACK(stack), confirmpage, "confirm");

	gtk_stack_set_transition_type(GTK_STACK(stack),
								  GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
	gtk_stack_set_transition_duration(GTK_STACK(stack), 200);

	GtkWidget* navbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_margin_start(navbar, 8);
	gtk_widget_set_margin_end(navbar, 8);
	gtk_widget_set_margin_top(navbar, 6);
	gtk_widget_set_margin_bottom(navbar, 6);
	wiz->navbar = navbar;

	GtkWidget* back_btn = gtk_button_new_with_label("Back");
	gtk_widget_set_visible(back_btn, FALSE);
	g_signal_connect(back_btn, "clicked", G_CALLBACK(cb_go_back), wiz);
	wiz->back_btn = back_btn;

	GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_hexpand(spacer, TRUE);

	GtkWidget* next_btn = gtk_button_new_with_label("Next");
	gtk_widget_set_visible(next_btn, FALSE);
	g_signal_connect(next_btn, "clicked", G_CALLBACK(cb_go_next), wiz);
	wiz->next_btn = next_btn;

	gtk_box_append(GTK_BOX(navbar), back_btn);
	gtk_box_append(GTK_BOX(navbar), spacer);
	gtk_box_append(GTK_BOX(navbar), next_btn);

	gtk_box_append(GTK_BOX(root), stack);
	gtk_box_append(GTK_BOX(root), navbar);

	gtk_window_set_child(GTK_WINDOW(gwnd), root);
	gtk_stack_set_visible_child_name(GTK_STACK(stack), "type");
	update_navbar(wiz);

	g_signal_connect_swapped(gwnd, "destroy", G_CALLBACK(g_free), wiz);

	gtk_window_present(GTK_WINDOW(gwnd));
}