#include <stdio.h>
#include <gtk/gtk.h>
#include "login.h"
#include "glib.h"
#include "imwnd.h"
#include "gtk/gtkshortcut.h"
#include "protocol/yamp.h"
#include "globals.h"
#include <cjson/cJSON.h>
#ifdef HAVE_LIBSECRET
#include <libsecret/secret.h>

const SecretSchema AppSchema = {
    "xyz.snow32.yampen",
    SECRET_SCHEMA_NONE,
    {
        {"userid", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {"server", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {NULL, 0}
    }
};
#endif
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <gio/gio.h>
typedef struct {
	GtkWidget* box;
	GtkWidget* entry;
	GtkWidget* popover;
	GtkWidget* list;
} EditableCombo;
// stuff on the registration page is prefixed reg_
GtkWidget* login_window;
GtkWidget* email_entry;
GtkWidget* reg_username_entry;
GtkWidget* reg_email_entry;
EditableCombo* server_combobox;
EditableCombo* reg_server_combobox;
GtkWidget* password_entry;
GtkWidget* reg_password_entry;
char* curUsername;
int mainfd;
SSL* mainsock;
static GtkApplication* login_app;
static int autoLoginPending;
static char* prefillUsername;
char* srvToSave;
static void BuildLoginWindow(GtkApplication* app);
#if HAVE_LIBSECRET
static char* pendingAuthToken;
#endif
#if HAVE_LIBSECRET
gboolean UnlockDefaultKeyring(void) {
	GError* error = NULL;

	SecretService* service =
		secret_service_get_sync(SECRET_SERVICE_LOAD_COLLECTIONS, NULL, &error);

	if (!service) {
		fprintf(stderr, "Secret Service: %s\n",
				error ? error->message : "unavailable");
		g_clear_error(&error);
		return FALSE;
	}

	SecretCollection* collection = secret_collection_for_alias_sync(
		service, "default", SECRET_COLLECTION_NONE, NULL, &error);

	if (!collection) {
		if (error) {
			fprintf(stderr, "Default keyring: %s\n", error->message);
		}

		g_clear_error(&error);
		g_object_unref(service);
		return FALSE;
	}

	gboolean success = TRUE;

	if (secret_collection_get_locked(collection)) {
		GList* targets = g_list_append(NULL, collection);
		GList* unlocked = NULL;

		gint count = secret_service_unlock_sync(service, targets, NULL,
												&unlocked, &error);

		success = count >= 0 && error == NULL;

		if (error) {
			fprintf(stderr, "Keyring unlock: %s\n", error->message);
			g_clear_error(&error);
		}

		g_list_free(unlocked);
		g_list_free(targets);
	}

	g_object_unref(collection);
	g_object_unref(service);

	return success;
}

gboolean SaveToken(const char* userid, const char* server,
                   const char* token)
{
    if (!userid || !*userid || !server || !*server ||
        !token || !*token)
        return FALSE;

    GError* error = NULL;
    gboolean ok = secret_password_store_sync(
        &AppSchema,
        SECRET_COLLECTION_DEFAULT,
        "Yampen session token",
        token,
        NULL,
        &error,
        "userid", userid,
        "server", server,
        NULL);

    if (!ok)
        fprintf(stderr, "Token store failed: %s\n",
                error ? error->message : "unknown error");

    g_clear_error(&error);
    return ok;
}

char* LoadToken(const char* userid, const char* server)
{
    if (!userid || !*userid || !server || !*server)
        return NULL;

    GError* error = NULL;
    gchar* token = secret_password_lookup_sync(
        &AppSchema, NULL, &error,
        "userid", userid,
        "server", server,
        NULL);

    if (error) {
        fprintf(stderr, "Token lookup failed: %s\n", error->message);
        g_clear_error(&error);
    }

    return token;
}

void ForgetToken(const char* userid, const char* server)
{
    if (!userid || !*userid || !server || !*server)
        return;

    GError* error = NULL;
    secret_password_clear_sync(
        &AppSchema, NULL, &error,
        "userid", userid,
        "server", server,
        NULL);

    if (error) {
        fprintf(stderr, "Token clear failed: %s\n", error->message);
        g_clear_error(&error);
    }
}

int SecretServiceAvail(void) {
	GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	if (!bus)
		return FALSE;

	GVariant* reply = g_dbus_connection_call_sync(
		bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
		"org.freedesktop.DBus", "NameHasOwner",
		g_variant_new("(s)", "org.freedesktop.secrets"), G_VARIANT_TYPE("(b)"),
		G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);

	g_object_unref(bus);

	if (!reply)
		return FALSE;

	gboolean owned;
	g_variant_get(reply, "(b)", &owned);
	g_variant_unref(reply);

	return owned;
}
#endif
static char* pendingRegisterUsername;
static char* pendingRegisterPassword;

gboolean CloseLoginDialog(gpointer data) {
	if (login_window != NULL) {
		gtk_window_destroy(GTK_WINDOW(login_window));
		login_window = NULL;
	}

	return G_SOURCE_REMOVE;
}

char* LastUserPath(void)
{
    char* dir = g_build_filename(
        g_get_user_config_dir(), "yampen", NULL);
    g_mkdir_with_parents(dir, 0700);

    char* path = g_build_filename(dir, "last_user", NULL);
    g_free(dir);
    return path;
}

gboolean SaveLastUser(const char* userid, const char* server)
{
    if (!userid || !*userid || !server || !*server)
        return FALSE;

    char* path = LastUserPath();

    char* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);

    char* contents = g_strdup_printf("%s@%s", userid, server);
    gboolean ok = g_file_set_contents(path, contents, -1, NULL);

    g_free(contents);
    g_free(dir);
    g_free(path);
    return ok;
}
typedef struct {
    char* id;
    char* server;
} UserSavedPayload;

UserSavedPayload LoadLastUser(void)
{
    UserSavedPayload payload = {0};
    char* path = LastUserPath();
    char* value = NULL;

    if (!g_file_get_contents(path, &value, NULL, NULL)) {
        g_free(path);
        return payload;
    }

    g_free(path);
    g_strstrip(value);

    char* sep = strchr(value, '@');

    if (!sep || sep == value || !sep[1]) {
        g_free(value);
        return payload;
    }

    *sep = '\0';
    payload.id = g_strdup(value);
    payload.server = g_strdup(sep + 1);

    g_free(value);
    return payload;
}

void FreeLastUser(UserSavedPayload payload)
{
    g_free(payload.id);
    g_free(payload.server);
}
gboolean FinishAuth(gpointer data) {
	g_application_release(G_APPLICATION(g_application_get_default()));
	StartMainIMWindow(data);
	free(data);
	CloseLoginDialog(NULL);
	return G_SOURCE_REMOVE;
}
gboolean AuthFail(gpointer data)
{
    if (autoLoginPending) {
#if HAVE_LIBSECRET
        UserSavedPayload saved = LoadLastUser();

        if (saved.id && saved.server)
            ForgetToken(saved.id, saved.server);

        FreeLastUser(saved);
#endif
        autoLoginPending = 0;

        if (!login_window)
            BuildLoginWindow(login_app);

        GtkAlertDialog* dialog =
            gtk_alert_dialog_new("Failed to authenticate\nRotated token?");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(login_window));
        g_object_unref(dialog);
        return G_SOURCE_REMOVE;
    }

    if (login_window) {
        GtkAlertDialog* dialog =
            gtk_alert_dialog_new("Failed to authenticate\nRotated token?");
        gtk_alert_dialog_show(dialog, GTK_WINDOW(login_window));
        g_object_unref(dialog);
    }

    return G_SOURCE_REMOVE;
}
gboolean ErrorOnRegisterFail(void* data) {
	GtkAlertDialog* dialog = gtk_alert_dialog_new("Failed to register!");
	return G_SOURCE_REMOVE;
}

void onYAMPAuth(const YampLoginData* dat)
{
    if (!dat) {
#if HAVE_LIBSECRET
        g_clear_pointer(&pendingAuthToken, g_free);
#endif
        g_idle_add_full(
            G_PRIORITY_DEFAULT, AuthFail, NULL, NULL);
        return;
    }

    if (srvToSave && *srvToSave)
        SaveLastUser(dat->usr.id, srvToSave);

#if HAVE_LIBSECRET
    if (pendingAuthToken) {
        if (srvToSave &&
            !SaveToken(dat->usr.id, srvToSave, pendingAuthToken)) {
            fprintf(stderr, "Warning: failed to save session token\n");
        }

        g_clear_pointer(&pendingAuthToken, g_free);
    }
#endif

    autoLoginPending = 0;

    g_idle_add_full(
        G_PRIORITY_DEFAULT, FinishAuth, (gpointer)dat, NULL);
}
void onYAMPRegisterResult(char* token, int success) {
    if (!success || !token) {
        g_idle_add_full(
            G_PRIORITY_DEFAULT, ErrorOnRegisterFail, NULL, NULL);
        return;
    }

#if HAVE_LIBSECRET
    g_free(pendingAuthToken);
    pendingAuthToken = g_strdup(token);
#endif

    YAMPAuth(mainsock, token, onYAMPAuth);
}
void cb_RegisterBtn(GtkWidget* self, gpointer UserData) {
	char* email = gtk_editable_get_chars(GTK_EDITABLE(reg_email_entry), 0, -1);
	char* username =
		gtk_editable_get_chars(GTK_EDITABLE(reg_username_entry), 0, -1);
	char* passwd =
		gtk_editable_get_chars(GTK_EDITABLE(reg_password_entry), 0, -1);
	char* server = (gtk_entry_buffer_get_text(
		gtk_entry_get_buffer(GTK_ENTRY(reg_server_combobox->entry))));
	if (!strlen(email)) {
		gtk_alert_dialog_show(gtk_alert_dialog_new("Please enter your email"),
							  GTK_WINDOW(login_window));
	}
	if (!strlen(username)) {
		gtk_alert_dialog_show(
			gtk_alert_dialog_new("Please fill out the username field"),
			GTK_WINDOW(login_window));
	}
	if (!strlen(passwd)) {
		gtk_alert_dialog_show(
			gtk_alert_dialog_new("Please fill out the password field"),
			GTK_WINDOW(login_window));
	}
	char* email_at = strchr(email, '@');
	char* email_tld = strchr(email_at, '.');
	if (!(email_at && email_tld)) {
		gtk_alert_dialog_show(
			gtk_alert_dialog_new("Please enter a valid email"),
			GTK_WINDOW(login_window));
	}
	int ok = YAMPConnect(server, &mainfd, &mainsock);
	if (ok < 0) {
		gtk_alert_dialog_show(
			gtk_alert_dialog_new("Failed to connect to the server!\nThe server "
								 "you have chosen might be down"),
			GTK_WINDOW(login_window));
	}
	srvToSave=server;
	YAMPRegister(mainsock, email, username, passwd, "Yampen @ Desktop", 0,
				 onYAMPRegisterResult);
}
void onYAMPLoginResult(char* token, int success) {
    if (!success || !token) {
        gtk_alert_dialog_show(
            gtk_alert_dialog_new(
                "Failure logging in!\nInvalid credentials?"),
            GTK_WINDOW(login_window));
        return;
    }

#if HAVE_LIBSECRET
    g_free(pendingAuthToken);
    pendingAuthToken = g_strdup(token);
#endif

    YAMPAuth(mainsock, token, onYAMPAuth);
}
GCallback cb_LoginBtn(GtkWidget* self, gpointer UserData) {
	printf("Logging in bleh\n");

	char* email = gtk_editable_get_chars(GTK_EDITABLE(email_entry), 0, -1);
	char* passwd = gtk_editable_get_chars(GTK_EDITABLE(password_entry), 0, -1);
	char* server = (gtk_entry_buffer_get_text(
		gtk_entry_get_buffer(GTK_ENTRY(server_combobox->entry))));
	if (!strlen(email)) {
		gtk_alert_dialog_show(gtk_alert_dialog_new("Please enter your email"),
							  GTK_WINDOW(login_window));
	}
	if (!strlen(passwd)) {
		gtk_alert_dialog_show(
			gtk_alert_dialog_new("Please fill out the password field"),
			GTK_WINDOW(login_window));
	}
	char* email_at = strchr(email, '@');
	char* email_tld = email_at ? strchr(email_at, '.') : NULL;
	if (!(email_at && email_tld)) {
		gtk_alert_dialog_show(
			gtk_alert_dialog_new("Please enter a valid email"),
			GTK_WINDOW(login_window));
	}
	int ok = YAMPConnect(server, &mainfd, &mainsock);
	if (ok < 0) {
		gtk_alert_dialog_show(
			gtk_alert_dialog_new("Failed to connect to the server!\nThe server "
								 "you have chosen might be down"),
			GTK_WINDOW(login_window));
	}
	YAMPLogin(mainsock, email, passwd, "Yampen @ PC", 0, onYAMPLoginResult);
	/*
	char* cpath = g_build_filename(g_get_user_config_dir(), "yampen", NULL);
	char* cfpath = g_build_filename(cpath, "last_user");
	GError* error = NULL;
	if (mkdir(cpath, 0755) == -1 && errno != EEXIST) {
		perror("mkdir");
	}

	int fd = open(cfpath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd == -1) {
		perror("open");
	}
	close(fd);
	*/


#if HAVE_LIBSECRET
	/*
	if (SecretServiceAvail) {
		int ok = secret_password_store_sync(
			&AppSchema, SECRET_COLLECTION_DEFAULT, "Yampen account password",
			password, NULL, &error, "username", uns, NULL);
		if (!ok) {
			fprintf(stderr, "secret store failed: %s\n",
					error ? error->message : "unknown error");

			if (error)
				g_error_free(error);
		} else {
			printf("password stored successfully\n");
		}
	}
	*/
#endif

	srvToSave=server;
}
gboolean ErrorOnLoginFail(gpointer data) {
	if (autoLoginPending && login_window == NULL) {
		autoLoginPending = 0;
		BuildLoginWindow(login_app);
	}
	GtkAlertDialog* dialog =
		gtk_alert_dialog_new("Username or password wrong!\n");
	gtk_alert_dialog_show(dialog, GTK_WINDOW(login_window));
	return G_SOURCE_REMOVE;
}
void onYAMPLoginFail() {
	g_idle_add_full(G_PRIORITY_LOW, ErrorOnLoginFail, NULL, NULL);
}

#if HAVE_LIBSECRET
typedef struct {
	char* uns;
	char* pwd;
} SavedCred;
int GetSavedLoginData(SavedCred* out) {
	char* cpath =
		g_build_filename(g_get_user_config_dir(), "yampen", "last_user", NULL);

	int fd = open(cpath, O_RDONLY, 0);
	if (fd == -1) {
		perror("open");
		return 0;
	}
	struct stat lustat;
	stat(cpath, &lustat);
	char* defusr = malloc(lustat.st_size + 1);

	read(fd, defusr, lustat.st_size);
	defusr[lustat.st_size] = '\0';
	out->uns = defusr;
	if (fd == -1) {
		printf("failed to open\n");
	}
	close(fd);
	GError* error = NULL;

	gchar* password =
		secret_password_lookup_sync(&AppSchema,
									NULL, // cancellable
									&error, "userid", defusr, NULL);
	if (password != NULL) {
		out->pwd = password;
	} else {
		printf("no passwd\n");
		return 0;
	}
	return 1;
}
#endif
void DisplayLoginDialog(GtkApplication* app)
{
    login_app = app;
    g_application_hold(G_APPLICATION(app));

#if HAVE_LIBSECRET
    if (SecretServiceAvail() && UnlockDefaultKeyring()) {
        UserSavedPayload saved = LoadLastUser();

        if (saved.id && saved.server) {
            int ok = YAMPConnect(saved.server, &mainfd, &mainsock);

            if (ok >= 0) {
                char* token = LoadToken(saved.id, saved.server);

                if (token) {
                    autoLoginPending = 1;

                    g_free(srvToSave);
                    srvToSave = g_strdup(saved.server);

                    YAMPAuth(mainsock, token, onYAMPAuth);

                    secret_password_free(token);
                    FreeLastUser(saved);
                    return;
                }
            }
        }

        FreeLastUser(saved);
    }
#endif

    BuildLoginWindow(app);
}

static void on_row_activated(GtkListBox* list, GtkListBoxRow* row,
							 gpointer data) {
	EditableCombo* ec = data;
	GtkWidget* label = gtk_list_box_row_get_child(row);
	gtk_editable_set_text(GTK_EDITABLE(ec->entry),
						  gtk_label_get_text(GTK_LABEL(label)));
	gtk_popover_popdown(GTK_POPOVER(ec->popover));
}

EditableCombo* editable_combo_new(const char* const* items) {
	EditableCombo* ec = g_new0(EditableCombo, 1);

	ec->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	ec->entry = gtk_entry_new();
	gtk_editable_set_text(GTK_EDITABLE(ec->entry), items[0]);
	ec->list = gtk_list_box_new();
	ec->popover = gtk_popover_new();

	gtk_widget_set_hexpand(ec->entry, TRUE);
	gtk_widget_add_css_class(ec->box, "linked");

	GtkWidget* scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), ec->list);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll),
											   250);
	gtk_scrolled_window_set_propagate_natural_height(
		GTK_SCROLLED_WINDOW(scroll), TRUE);
	gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll),
													TRUE);
	gtk_popover_set_child(GTK_POPOVER(ec->popover), scroll);

	GtkWidget* btn = gtk_menu_button_new();
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn), ec->popover);

	gtk_box_append(GTK_BOX(ec->box), ec->entry);
	gtk_box_append(GTK_BOX(ec->box), btn);

	for (; items && *items; items++) {
		GtkWidget* l = gtk_label_new(*items);
		gtk_label_set_xalign(GTK_LABEL(l), 0.0);
		gtk_list_box_append(GTK_LIST_BOX(ec->list), l);
	}

	g_signal_connect(ec->list, "row-activated", G_CALLBACK(on_row_activated),
					 ec);
	return ec;
}
const char* const initial_srvs[] = {
	"yamp.root.sx",
	"dustbox.urani.de",
	NULL,
};
GtkWidget* pagestack;
gboolean OnPageSwapLink(GtkLabel* label, const char* uri, gpointer user_data) {
	if (strcmp(uri, "register-pane") == 0) {
		gtk_stack_set_visible_child_name(GTK_STACK(pagestack), "register");
	} else if (strcmp(uri, "login-pane") == 0) {
		gtk_stack_set_visible_child_name(GTK_STACK(pagestack), "login");
	}
	return 1;
}
void BuildLoginWindow(GtkApplication* app) {
	login_window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(login_window), "Yampen - Login");
	gtk_window_set_default_size(GTK_WINDOW(login_window), 800, 400);

	// maaaain horizontal box to split the window
	GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_window_set_child(GTK_WINDOW(login_window), main_box);

	// left branding side
	GtkWidget* titlebox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_hexpand(titlebox, TRUE);
	gtk_widget_set_halign(titlebox, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(titlebox, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(titlebox, 20);
	gtk_widget_set_margin_end(titlebox, 20);
	gtk_box_append(GTK_BOX(main_box), titlebox);

	// title
	GtkWidget* title = gtk_label_new("Yampen");
	PangoAttrList* attrs = pango_attr_list_new();
	pango_attr_list_insert(attrs, pango_attr_size_new(32 * PANGO_SCALE));
	pango_attr_list_insert(attrs,
						   pango_attr_weight_new(PANGO_WEIGHT_ULTRABOLD));
	gtk_label_set_attributes(GTK_LABEL(title), attrs);
	gtk_box_append(GTK_BOX(titlebox), title);

	// subtitle
	GtkWidget* subtitle =
		gtk_label_new("The official GTK4 client of the YAMP protocol");
	gtk_widget_set_opacity(subtitle, 0.7);
	gtk_box_append(GTK_BOX(titlebox), subtitle);

	pagestack = gtk_stack_new();
	gtk_stack_set_transition_type(GTK_STACK(pagestack),
								  GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
	gtk_stack_set_transition_duration(GTK_STACK(pagestack), 200);

	GtkWidget* loginbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
	gtk_widget_set_hexpand(loginbox, TRUE);
	gtk_widget_set_halign(loginbox, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(loginbox, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(loginbox, 20);
	gtk_widget_set_margin_end(loginbox, 20);
	gtk_widget_set_margin_top(loginbox, 24);
	gtk_widget_set_margin_bottom(loginbox, 24);

	GtkWidget* registerbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
	gtk_widget_set_hexpand(registerbox, TRUE);
	gtk_widget_set_halign(registerbox, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(registerbox, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(registerbox, 20);
	gtk_widget_set_margin_end(registerbox, 20);
	gtk_widget_set_margin_top(registerbox, 24);
	gtk_widget_set_margin_bottom(registerbox, 24);
	gtk_stack_add_named(GTK_STACK(pagestack), loginbox, "login");
	gtk_stack_add_named(GTK_STACK(pagestack), registerbox, "register");
	gtk_box_append(GTK_BOX(main_box), pagestack);

	/* BELOW THIS IS SETUP FOR THE LOGIN PAGE (THE SETUP OF REGISTRING IS BELOW,
	 * ALSO MARKED WITH A COMMENT) */
	// login header
	GtkWidget* login_header = gtk_label_new("Sign In");
	PangoAttrList* header_attrs = pango_attr_list_new();
	pango_attr_list_insert(header_attrs, pango_attr_size_new(18 * PANGO_SCALE));
	pango_attr_list_insert(header_attrs,
						   pango_attr_weight_new(PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes(GTK_LABEL(login_header), header_attrs);
	pango_attr_list_unref(header_attrs);
	gtk_box_append(GTK_BOX(loginbox), login_header);

	// username & server entry areaa
	GtkWidget* email_label = gtk_label_new("Email:");
	gtk_widget_set_halign(email_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(loginbox), email_label);

	email_entry = gtk_entry_new();
	gtk_box_append(GTK_BOX(loginbox), email_entry);
	gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "Enter your email");

	GtkWidget* server_label = gtk_label_new("Server:");
	gtk_widget_set_halign(server_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(loginbox), server_label);

	server_combobox = editable_combo_new(initial_srvs);

	gtk_box_append(GTK_BOX(loginbox), server_combobox->box);

	// passworb box
	GtkWidget* password_label = gtk_label_new("Password:");
	gtk_widget_set_halign(password_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(loginbox), password_label);

	password_entry = gtk_password_entry_new();
	gtk_widget_set_size_request(password_entry, 250, -1);
	gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(password_entry),
										  TRUE);
	gtk_box_append(GTK_BOX(loginbox), password_entry);

	GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_hexpand(button_box, TRUE);
	gtk_widget_set_margin_top(button_box, 10);
	gtk_box_append(GTK_BOX(loginbox), button_box);

	// login button
	GtkWidget* login_button = gtk_button_new_with_label("Login");
	gtk_widget_set_hexpand(login_button, TRUE);
	gtk_box_append(GTK_BOX(button_box), login_button);
	g_signal_connect(login_button, "clicked", G_CALLBACK(cb_LoginBtn), 0);

	GtkWidget* register_link = gtk_label_new(NULL);
	gtk_label_set_markup(
		GTK_LABEL(register_link),
		"<a href=\"register-pane\">No accounts yet? Register here</a>");
	g_signal_connect(register_link, "activate-link", G_CALLBACK(OnPageSwapLink),
					 NULL);
	gtk_box_append(GTK_BOX(loginbox), register_link);

	/* BELOW THIS IS REGISTER PAGE, LOOK ABOVE FOR THE LOGIN ONE */

	GtkWidget* register_header = gtk_label_new("Register");
	gtk_label_set_attributes(GTK_LABEL(register_header), header_attrs);
	gtk_box_append(GTK_BOX(registerbox), register_header);
	// username & server entry areaa
	GtkWidget* reg_email_label = gtk_label_new("Email:");
	gtk_widget_set_halign(reg_email_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(registerbox), reg_email_label);

	reg_email_entry = gtk_entry_new();
	gtk_box_append(GTK_BOX(registerbox), reg_email_entry);
	gtk_entry_set_placeholder_text(GTK_ENTRY(reg_email_entry),
								   "Enter your email");

	GtkWidget* reg_username_label = gtk_label_new("Username:");
	gtk_widget_set_halign(reg_username_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(registerbox), reg_username_label);

	reg_username_entry = gtk_entry_new();
	gtk_box_append(GTK_BOX(registerbox), reg_username_entry);
	gtk_entry_set_placeholder_text(GTK_ENTRY(reg_username_entry),
								   "Enter your initial username");

	GtkWidget* reg_server_label = gtk_label_new("Server:");
	gtk_widget_set_halign(reg_server_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(registerbox), reg_server_label);

	reg_server_combobox = editable_combo_new(initial_srvs);
	gtk_box_append(GTK_BOX(registerbox), reg_server_combobox->box);

	// passworb box
	GtkWidget* reg_password_label = gtk_label_new("Password:");
	gtk_widget_set_halign(reg_password_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(registerbox), reg_password_label);

	reg_password_entry = gtk_password_entry_new();
	gtk_widget_set_size_request(reg_password_entry, 250, -1);
	gtk_password_entry_set_show_peek_icon(
		GTK_PASSWORD_ENTRY(reg_password_entry), TRUE);
	gtk_box_append(GTK_BOX(registerbox), reg_password_entry);

	GtkWidget* reg_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_hexpand(reg_button_box, TRUE);
	gtk_widget_set_margin_top(reg_button_box, 10);
	gtk_box_append(GTK_BOX(registerbox), reg_button_box);

	GtkWidget* register_button = gtk_button_new_with_label("Register");
	gtk_widget_set_hexpand(register_button, TRUE);
	gtk_box_append(GTK_BOX(reg_button_box), register_button);
	g_signal_connect(register_button, "clicked", G_CALLBACK(cb_RegisterBtn), 0);

	GtkWidget* login_link = gtk_label_new(NULL);
	gtk_label_set_markup(
		GTK_LABEL(login_link),
		"<a href=\"login-pane\">Already got an account? Login here</a>");
	g_signal_connect(login_link, "activate-link", G_CALLBACK(OnPageSwapLink),
					 NULL);
	gtk_box_append(GTK_BOX(registerbox), login_link);


	gtk_window_present(GTK_WINDOW(login_window));
}
