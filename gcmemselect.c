#include "globals.h"
#include "protocol/yamp.h"
#include <gtk/gtk.h>

typedef struct {
    GtkWidget* wnd;
    GtkWidget* memberlist;
    void (*callback)(YampUser* users, int nusers);
} GCInitSelectorCtx;

void OnGCInitMemberSelectorConfirm(GtkButton* button, gpointer data){
    GCInitSelectorCtx* ctx = data;

    int total = 0;
    while(gtk_list_box_get_row_at_index(GTK_LIST_BOX(ctx->memberlist), total)) total++;

    YampUser* selected = g_new0(YampUser, total ? total : 1);
    int nselected = 0;

    for(int i = 0; i < total; i++){
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ctx->memberlist), i);
        GtkWidget* check = gtk_list_box_row_get_child(row);
        if(gtk_check_button_get_active(GTK_CHECK_BUTTON(check))){
            YampUser* user = g_object_get_data(G_OBJECT(row), "user");
            selected[nselected++] = *user;
        }
    }

    if(nselected == 0){
        g_free(selected);
        return;
    }

    if(ctx->callback) ctx->callback(selected, nselected);
    g_free(selected);
    gtk_window_destroy(GTK_WINDOW(ctx->wnd));
}

void SpawnGCInitMemberSelector(YampUser* content, int ncontent, void (*callback)(YampUser* users, int nusers)){
    GtkWidget* wnd = gtk_application_window_new(global_app);
    gtk_window_set_default_size(GTK_WINDOW(wnd), 300, 400);

    GCInitSelectorCtx* ctx = g_new0(GCInitSelectorCtx, 1);
    ctx->wnd = wnd;
    ctx->callback = callback;
    g_signal_connect_swapped(wnd, "destroy", G_CALLBACK(g_free), ctx);

    GtkWidget* mainvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* titlebar = gtk_label_new(NULL);
    gtk_label_set_markup(
        GTK_LABEL(titlebar),
        "<span size=\"large\" weight=\"bold\">Choose the initial members</span>");
    gtk_box_append(GTK_BOX(mainvbox), titlebar);

    GtkWidget* memberlist = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(memberlist), GTK_SELECTION_NONE);
    ctx->memberlist = memberlist;

    for(int i = 0; i < ncontent; i++){
        GtkWidget* userrow = gtk_list_box_row_new();
        GtkWidget* check = gtk_check_button_new_with_label(
            content[i].displayname ? content[i].displayname : content[i].username);
        g_object_set_data(G_OBJECT(userrow), "user", &content[i]);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(userrow), check);
        gtk_list_box_append(GTK_LIST_BOX(memberlist), userrow);
    }

    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), memberlist);
    gtk_box_append(GTK_BOX(mainvbox), scroller);

    GtkWidget* buttonbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(buttonbox, GTK_ALIGN_END);
    gtk_widget_set_margin_top(buttonbox, 6);
    gtk_widget_set_margin_bottom(buttonbox, 6);
    gtk_widget_set_margin_end(buttonbox, 6);

    GtkWidget* confirmbtn = gtk_button_new_with_label("Create");
    g_signal_connect(confirmbtn, "clicked", G_CALLBACK(OnGCInitMemberSelectorConfirm), ctx);
    gtk_box_append(GTK_BOX(buttonbox), confirmbtn);

    gtk_box_append(GTK_BOX(mainvbox), buttonbox);
    gtk_window_set_child(GTK_WINDOW(wnd), mainvbox);
    gtk_window_present(GTK_WINDOW(wnd));
}