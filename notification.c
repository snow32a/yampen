#include <gtk/gtk.h>
void ClientNotificationCloseCB(GtkButton* btn, void* data){
    gtk_widget_unparent(GTK_WIDGET(data));
}
GtkWidget* CreateClientNotification(GtkWidget* overlay, char* title, GtkWidget* content) {
    GtkWidget* notifbox = gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
	GtkWidget* headerbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(notifbox, "app-notification");
	GtkWidget* label = gtk_label_new(title);
	gtk_box_append(GTK_BOX(headerbox), label);
	g_object_set_data(G_OBJECT(headerbox), "label", label);

	GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic");
	gtk_widget_add_css_class(close, "flat");
	gtk_widget_add_css_class(close, "circular");
    g_signal_connect(close,"clicked",G_CALLBACK(ClientNotificationCloseCB),notifbox);
	gtk_box_append(GTK_BOX(headerbox), close);

	gtk_box_append(GTK_BOX(notifbox), headerbox);
	gtk_box_append(GTK_BOX(notifbox), content);

	gtk_widget_set_halign(notifbox, GTK_ALIGN_END);
	gtk_widget_set_valign(notifbox, GTK_ALIGN_END);
	gtk_widget_set_margin_end(notifbox, 16);
	gtk_widget_set_margin_bottom(notifbox, 16);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay),notifbox);
	return notifbox;
}