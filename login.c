#include <stdio.h>
#include <gtk/gtk.h>
#include "login.h"
#include "imwnd.h"
#include "gtk/gtkshortcut.h"
#include "protocol/yamp.h"
#include "globals.h"
#include <libsecret/secret.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <openssl/ssl.h>
const SecretSchema AppSchema = {
	"xyz.snow32.yampen",
	SECRET_SCHEMA_NONE,
	{{"username", SECRET_SCHEMA_ATTRIBUTE_STRING}, {NULL, 0}}};
GtkWidget *login_window;
GtkWidget *username_entry;
GtkWidget *password_entry;
char *curUsername;
int mainfd;
SSL* mainsock;
int NwLogin(char *uns, char *password) {
	char *username;
	char *server;
	if (strlen(password) < 1) {
		GtkAlertDialog *dialog =
			gtk_alert_dialog_new("You need to enter your password");
		gtk_alert_dialog_show(dialog, GTK_WINDOW(login_window));
	}
	if (!(SplitAddress(uns, &username, &server) && strlen(username) > 0 &&
		  strlen(server) > 0)) {
		GtkAlertDialog *dialog = gtk_alert_dialog_new(
			"Your user format seems incorrect, YAMP uses user@server.com style "
			"IDs");
		gtk_alert_dialog_show(dialog, GTK_WINDOW(login_window));
		return 0;
	}
	int mainfd;
	int ConnectStatus = YAMPConnect(server, &mainfd, &mainsock);
	if (ConnectStatus < 0) {
		GtkAlertDialog *dialog = gtk_alert_dialog_new(
			"Failed connecting to the specified server!\n");
		gtk_alert_dialog_show(dialog, GTK_WINDOW(login_window));
		return 0;
	} else {
		printf("success connecting\n");
	}
	YAMPLogin(mainsock, username, password);
	curUsername = strdup(username);
}
GCallback cb_LoginBtn(GtkWidget *self, gpointer UserData) {
	printf("Logging in bleh\n");
	char *uns = strdup(gtk_entry_buffer_get_text(
		gtk_entry_get_buffer(GTK_ENTRY(username_entry))));
	char *password = strdup(gtk_entry_buffer_get_text(
		gtk_entry_get_buffer(GTK_ENTRY(password_entry))));
	char *cfpath =
		g_build_filename(g_get_user_config_dir(), "yampen", "last_user", NULL);
	char *cpath = g_build_filename(g_get_user_config_dir(), "yampen", NULL);

	if (mkdir(cpath, 0755) == -1 && errno != EEXIST) {
		perror("mkdir");
	}

	int fd = open(cfpath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd == -1) {
		perror("open");
	}
	write(fd, uns, strlen(uns));
	close(fd);
	NwLogin(uns, password);
	GError *error = NULL;
#if HAVE_LIBSECRET
	int ok = secret_password_store_sync(&AppSchema, SECRET_COLLECTION_DEFAULT,
										"Yampen account password", password,
										NULL, &error, "username", uns, NULL);
	if (!ok) {
		fprintf(stderr, "secret store failed: %s\n",
				error ? error->message : "unknown error");

		if (error)
			g_error_free(error);
	} else {
		printf("password stored successfully\n");
	}
#endif
	free(uns);
	free(password);
}
gboolean CloseLoginDialog(gpointer data) {
	if (login_window != NULL) {
		gtk_window_destroy(GTK_WINDOW(login_window));
		login_window = NULL;
	}

	return G_SOURCE_REMOVE;
}
gboolean DoLoggedIn(gpointer data) {
	g_application_release(G_APPLICATION(g_application_get_default()));
	YAMPListBuddies(mainsock);
	StartMainIMWindow();
	CloseLoginDialog(NULL);
	return G_SOURCE_REMOVE;
}

void onYAMPLoggedIn() {
	g_idle_add_full(G_PRIORITY_DEFAULT, DoLoggedIn, NULL, NULL);
}
gboolean ErrorOnLoginFail(gpointer data) {
	GtkAlertDialog *dialog =
		gtk_alert_dialog_new("Username or password wrong!\n");
	gtk_alert_dialog_show(dialog, GTK_WINDOW(login_window));
	return G_SOURCE_REMOVE;
}
void onYAMPLoginFail() {
	g_idle_add_full(G_PRIORITY_LOW, ErrorOnLoginFail, NULL, NULL);
}

#if HAVE_LIBSECRET
typedef struct {
	char *uns;
	char *pwd;
} SavedCred;
int GetSavedLoginData(SavedCred *out) {
	char *cpath =
		g_build_filename(g_get_user_config_dir(), "yampen", "last_user", NULL);

	int fd = open(cpath, O_RDONLY, 0);
	if (fd == -1) {
		perror("open");
		return 0;
	}
	struct stat lustat;
	stat(cpath, &lustat);
	char *defusr = malloc(lustat.st_size + 1);

	read(fd, defusr, lustat.st_size);
	defusr[lustat.st_size] = '\0';
	out->uns = defusr;
	if (fd == -1) {
		printf("failed to open\n");
	}
	close(fd);
	GError *error = NULL;
	gchar *password =
		secret_password_lookup_sync(&AppSchema,
									NULL, // cancellable
									&error, "username", defusr, NULL);
	if (password != NULL) {
		out->pwd = password;
	} else {
		printf("no passwd\n");
		return 0;
	}
	return 1;
}
#endif
void DisplayLoginDialog(GtkApplication *app) {
	g_application_hold(G_APPLICATION(app));
#if HAVE_LIBSECRET
	SavedCred cred;
	if (GetSavedLoginData(&cred)) {
		NwLogin(cred.uns, cred.pwd);
		free(cred.uns);
		secret_password_free(cred.pwd);
		return;
	}
#endif
	login_window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(login_window), "Yampen - Login");
	gtk_window_set_default_size(GTK_WINDOW(login_window), 600, 400);

	// maaaain horizontal box to split the window
	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_window_set_child(GTK_WINDOW(login_window), main_box);

	// left branding side
	GtkWidget *titlebox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_hexpand(titlebox, TRUE);
	gtk_widget_set_halign(titlebox, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(titlebox, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(titlebox, 20);
	gtk_widget_set_margin_end(titlebox, 20);
	gtk_box_append(GTK_BOX(main_box), titlebox);

	// title
	GtkWidget *title = gtk_label_new("Yampen");
	PangoAttrList *attrs = pango_attr_list_new();
	pango_attr_list_insert(attrs, pango_attr_size_new(32 * PANGO_SCALE));
	pango_attr_list_insert(attrs,
						   pango_attr_weight_new(PANGO_WEIGHT_ULTRABOLD));
	gtk_label_set_attributes(GTK_LABEL(title), attrs);
	pango_attr_list_unref(attrs);
	gtk_box_append(GTK_BOX(titlebox), title);

	// subtitle
	GtkWidget *subtitle = gtk_label_new(
		"The official client of the Yet Another Messaging Protocol");
	gtk_widget_set_opacity(subtitle, 0.7);
	gtk_box_append(GTK_BOX(titlebox), subtitle);

	// login form type shi
	GtkWidget *loginbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
	gtk_widget_set_hexpand(loginbox, TRUE);
	gtk_widget_set_halign(loginbox, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(loginbox, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(loginbox, 20);
	gtk_widget_set_margin_end(loginbox, 20);
	gtk_box_append(GTK_BOX(main_box), loginbox);

	// login header
	GtkWidget *login_header = gtk_label_new("Sign In");
	PangoAttrList *header_attrs = pango_attr_list_new();
	pango_attr_list_insert(header_attrs, pango_attr_size_new(18 * PANGO_SCALE));
	pango_attr_list_insert(header_attrs,
						   pango_attr_weight_new(PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes(GTK_LABEL(login_header), header_attrs);
	pango_attr_list_unref(header_attrs);
	gtk_box_append(GTK_BOX(loginbox), login_header);

	// username & server entry areaa
	GtkWidget *username_label = gtk_label_new("Username & Server:");
	gtk_widget_set_halign(username_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(loginbox), username_label);

	username_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(username_entry),
								   "user@example.com");
	gtk_widget_set_size_request(username_entry, 250, -1);
	gtk_box_append(GTK_BOX(loginbox), username_entry);

	// passworb box
	GtkWidget *password_label = gtk_label_new("Password:");
	gtk_widget_set_halign(password_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(loginbox), password_label);

	password_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry),
								   "Enter your password");
	gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
	gtk_entry_set_input_purpose(GTK_ENTRY(password_entry),
								GTK_INPUT_PURPOSE_PASSWORD);
	gtk_widget_set_size_request(password_entry, 250, -1);
	gtk_box_append(GTK_BOX(loginbox), password_entry);

	// button box (stole ts lwk)
	GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top(button_box, 10);
	gtk_box_append(GTK_BOX(loginbox), button_box);

	// login button
	GtkWidget *login_button = gtk_button_new_with_label("Login");
	gtk_widget_set_size_request(login_button, 100, -1);
	gtk_box_append(GTK_BOX(button_box), login_button);
	g_signal_connect(login_button, "clicked", G_CALLBACK(cb_LoginBtn), 0);
	// register button
	GtkWidget *register_button = gtk_button_new_with_label("Register");
	gtk_widget_set_size_request(register_button, 100, -1);
	gtk_box_append(GTK_BOX(button_box), register_button);

	gtk_window_present(GTK_WINDOW(login_window));
}
