#include <yamp.h>
#include <gtk/gtk.h>
#include <globals.h>
#include <imwnd.h>
#include <imwnd_parts.h>
#include <avatar.h>
#include <hashtables.h>
#include <memberslist.h>

MemberSet* MemberSetCopy(YampUser** src, int n) {
	MemberSet* set = malloc(sizeof(MemberSet));
	set->n = n;
	set->users = calloc(n ? n : 1, sizeof(YampUser*));
	for (int i = 0; i < n; i++) {
		set->users[i] = DupUserObject(src[i]);
	}
	return set;
}

void MemberSetFree(gpointer data) {
	MemberSet* set = data;
	if (!set)
		return;
	for (int i = 0; i < set->n; i++) {
		FreeUserObject(set->users[i]);
	}
	free(set->users);
	free(set);
}

enum { ACTION_KICK = 1, ACTION_TRANSFER = 2 };

static GtkWidget* memberhdr = NULL;
static int membercount = 0;
static char* member_owner = NULL;

static MemberActionCb kick_cb = NULL;
static MemberActionCb transfer_cb = NULL;
static void* action_ud = NULL;

static gboolean IsOwnerId(const char* id) {
    return id && member_owner && strcmp(id, member_owner) == 0;
}

static void UpdateMemberHeader(void) {
    if (!memberhdr) return;
    char* t = g_strdup_printf("Members - (%d/%d)", membercount,YAMPGetMaxGCMembers());
    gtk_label_set_text(GTK_LABEL(memberhdr), t);
    g_free(t);
}

static void FreeRowUser(gpointer p) {
    FreeUserObject(p);
}

static gboolean CanManageMember(const YampUser* u) {
    return curUserID && IsOwnerId(curUserID) && strcmp(u->id, curUserID) != 0;
}

static void OnMenuAction(GtkButton* btn, gpointer row_ptr) {
    GtkWidget* row = GTK_WIDGET(row_ptr);
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "action"));

    GtkWidget* pop = g_object_get_data(G_OBJECT(row), "ctx-popover");
    if (pop) gtk_popover_popdown(GTK_POPOVER(pop));

    YampUser* src = g_object_get_data(G_OBJECT(row), "user");
    MemberActionCb cb = (action == ACTION_KICK) ? kick_cb : transfer_cb;
    if (!src || !cb) return;

    YampUser* tmp = DupUserObject(src);
    cb(tmp, action_ud);
    FreeUserObject(tmp);
}

static GtkWidget* BuildMenuButton(const char* label, int action,
                                  GtkWidget* row) {
    GtkWidget* b = gtk_button_new_with_label(label);
    gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
    gtk_widget_set_halign(gtk_button_get_child(GTK_BUTTON(b)), GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(b), "action", GINT_TO_POINTER(action));
    g_signal_connect(b, "clicked", G_CALLBACK(OnMenuAction), row);
    return b;
}

static GMenuModel* member_menu_model = NULL;

// lazy loading
static GMenuModel* GetMemberMenuModel(void) {
    if (member_menu_model) return member_menu_model;

    GMenu* m = g_menu_new();

    GMenuItem* kick = g_menu_item_new("Kick", "member.kick");
    g_menu_item_set_attribute(kick, "hidden-when", "s", "action-disabled");
    g_menu_append_item(m, kick);
    g_object_unref(kick);

    GMenuItem* xfer = g_menu_item_new("Transfer Ownership", "member.transfer");
    g_menu_item_set_attribute(xfer, "hidden-when", "s", "action-disabled");
    g_menu_append_item(m, xfer);
    g_object_unref(xfer);

    member_menu_model = G_MENU_MODEL(m);
    return member_menu_model;
}
static void RunMemberAction(GtkWidget* row, int action) {
    YampUser* src = g_object_get_data(G_OBJECT(row), "user");
    MemberActionCb cb = (action == ACTION_KICK) ? kick_cb : transfer_cb;
    if (!src || !cb) return;

    YampUser* tmp = DupUserObject(src);
    cb(tmp, action_ud);
    FreeUserObject(tmp);
}

static void OnKickActivate(GSimpleAction* a, GVariant* p, gpointer row) {
    (void)a; (void)p;
    RunMemberAction(GTK_WIDGET(row), ACTION_KICK);
}

static void OnTransferActivate(GSimpleAction* a, GVariant* p, gpointer row) {
    (void)a; (void)p;
    RunMemberAction(GTK_WIDGET(row), ACTION_TRANSFER);
}

static void ShowMemberMenu(GtkWidget* row, double x, double y) {
    YampUser* u = g_object_get_data(G_OBJECT(row), "user");
    GtkWidget* pop = g_object_get_data(G_OBJECT(row), "ctx-popover");
    if (!u || !pop || !CanManageMember(u)) return;
    if (!kick_cb && !transfer_cb) return;

    g_simple_action_set_enabled(g_object_get_data(G_OBJECT(row), "kick-action"),
                                kick_cb != NULL);
    g_simple_action_set_enabled(g_object_get_data(G_OBJECT(row), "xfer-action"),
                                transfer_cb != NULL);

    GdkRectangle r = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(pop), &r);
    gtk_popover_popup(GTK_POPOVER(pop));
}

static void OnRowRightClick(GtkGestureClick* g, int n_press, double x,
                            double y, gpointer row) {
    (void)g; (void)n_press;
    ShowMemberMenu(GTK_WIDGET(row), x, y);
}

static void OnRowLongPress(GtkGestureLongPress* g, double x, double y,
                           gpointer row) {
    (void)g;
    ShowMemberMenu(GTK_WIDGET(row), x, y);
}
static void AttachMemberMenu(GtkWidget* row) {
    GSimpleActionGroup* grp = g_simple_action_group_new();

    GSimpleAction* kick = g_simple_action_new("kick", NULL);
    g_signal_connect(kick, "activate", G_CALLBACK(OnKickActivate), row);
    g_action_map_add_action(G_ACTION_MAP(grp), G_ACTION(kick));
    g_object_unref(kick);   // the group owns it now

    GSimpleAction* xfer = g_simple_action_new("transfer", NULL);
    g_signal_connect(xfer, "activate", G_CALLBACK(OnTransferActivate), row);
    g_action_map_add_action(G_ACTION_MAP(grp), G_ACTION(xfer));
    g_object_unref(xfer);

    gtk_widget_insert_action_group(row, "member", G_ACTION_GROUP(grp));
    g_object_unref(grp);    // the row owns it now

    GtkWidget* pop = gtk_popover_menu_new_from_model(GetMemberMenuModel());
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);
    gtk_widget_set_halign(pop, GTK_ALIGN_START);
    gtk_widget_set_parent(pop, row);
    g_signal_connect_swapped(row, "destroy", G_CALLBACK(gtk_widget_unparent), pop);

    g_object_set_data(G_OBJECT(row), "ctx-popover", pop);
    g_object_set_data(G_OBJECT(row), "kick-action", kick);
    g_object_set_data(G_OBJECT(row), "xfer-action", xfer);


    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click),
                                  GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(OnRowRightClick), row);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(click));

    GtkGesture* lp = gtk_gesture_long_press_new();
    gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(lp), TRUE);
    g_signal_connect(lp, "pressed", G_CALLBACK(OnRowLongPress), row);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(lp));
}



void SetMemberListActionCallbacks(MemberActionCb kick, MemberActionCb transfer,
                              void* userdata) {
    kick_cb = kick;
    transfer_cb = transfer;
    action_ud = userdata;
}

static GtkWidget* BuildMemberRow(const YampUser* u) {
    const char* name = u->displayname ? u->displayname : u->username;

    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget* pfp;
    if (u->pfp && *u->pfp) {
        pfp = gtk_image_new();
        char* url = ProcessAttachmentURL(u->pfp);
        char* pfp_path = AvatarCachePath(url);
        LoadAvatarAsync(pfp, url, pfp_path, 32);
        InsertPfpPath(u->id, pfp_path);
        free(url);
        g_free(pfp_path);
    } else {
        char* respath = g_strdup_printf(
            "/org/yampen/assets/pfps/default%d.png", GetDefaultPfp(u->id));
        pfp = gtk_image_new_from_resource(respath);
        g_free(respath);
    }
    gtk_widget_add_css_class(pfp, "avatar");

    const char* stat = (u->status && u->status->status) ? u->status->status
                                                        : "offline";
    GtkWidget* frame = WrapInStatusFrame(pfp, 32, StatusToColor(stat));
    g_object_set_data_full(G_OBJECT(frame), "user-id", g_strdup(u->id), g_free);

    char* label_txt = (curUserID && strcmp(u->id, curUserID) == 0)
                          ? g_strdup_printf("%s (you)", name)
                          : g_strdup(name);
    GtkWidget* lbl = gtk_label_new(label_txt);
    g_free(label_txt);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_hexpand(lbl, TRUE);

    gtk_box_append(GTK_BOX(box), frame);
    gtk_box_append(GTK_BOX(box), lbl);

    GtkWidget* crown = gtk_image_new_from_icon_name("crown-symbolic");
    gtk_widget_set_visible(crown, IsOwnerId(u->id));
    gtk_box_append(GTK_BOX(box), crown);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

    g_object_set_data_full(G_OBJECT(row), "user-id", g_strdup(u->id), g_free);
    g_object_set_data_full(G_OBJECT(row), "user", DupUserObject((YampUser*)u),
                           FreeRowUser);
    g_object_set_data(G_OBJECT(row), "crown", crown);

    AttachMemberMenu(row);
    return row;
}

static GtkListBoxRow* FindMemberRowIdx(const char* user_id, int* idx_out) {
    if (!memberlist || !user_id) return NULL;
    for (int i = 0;; i++) {
        GtkListBoxRow* r = gtk_list_box_get_row_at_index(
            GTK_LIST_BOX(memberlist), i);
        if (!r) return NULL;
        const char* id = g_object_get_data(G_OBJECT(r), "user-id");
        if (id && strcmp(id, user_id) == 0) {
            if (idx_out) *idx_out = i;
            return r;
        }
    }
}

static GtkListBoxRow* FindMemberRow(const char* user_id) {
    return FindMemberRowIdx(user_id, NULL);
}

// PUBLIC API
int AppendMember(const YampUser* u, const char* owner_id) {
    if (!u || !memberlist) return 0;
    if (owner_id) SetMemberOwner(owner_id);
    if (FindMemberRow(u->id)) return 0;

    gtk_list_box_append(GTK_LIST_BOX(memberlist), BuildMemberRow(u));
    membercount++;
    UpdateMemberHeader();
    return 1;
}

int RemoveMember(const char* user_id) {
    GtkListBoxRow* r = FindMemberRow(user_id);
    if (!r) return 0;

    gtk_list_box_remove(GTK_LIST_BOX(memberlist), GTK_WIDGET(r));
    if (membercount > 0) membercount--;
    UpdateMemberHeader();
    return 1;
}

int UpdateMember(const YampUser* u) {
    int idx = -1;
    GtkListBoxRow* r = u ? FindMemberRowIdx(u->id, &idx) : NULL;
    if (!r) return 0;

    gtk_list_box_remove(GTK_LIST_BOX(memberlist), GTK_WIDGET(r));
    gtk_list_box_insert(GTK_LIST_BOX(memberlist), BuildMemberRow(u), idx);
    return 1;
}

int SetMemberOwner(const char* new_owner_id) {
    if (!new_owner_id) return 0;
    if (member_owner && strcmp(member_owner, new_owner_id) == 0) return 0;

    g_free(member_owner);
    member_owner = g_strdup(new_owner_id);

    if (memberlist) {
        for (int i = 0;; i++) {
            GtkListBoxRow* r = gtk_list_box_get_row_at_index(
                GTK_LIST_BOX(memberlist), i);
            if (!r) break;
            GtkWidget* crown = g_object_get_data(G_OBJECT(r), "crown");
            const char* id = g_object_get_data(G_OBJECT(r), "user-id");
            if (crown) gtk_widget_set_visible(crown, IsOwnerId(id));
        }
    }
    return 1;
}

const char* GetMemberOwner(void) {
    return member_owner;
}

int IsLocalUserOwner(void) {
    return curUserID && IsOwnerId(curUserID);
}

int GetMemberCount(void) {
    return membercount;
}

void ResetGCMemberList(void) {
    memberhdr = NULL;
    memberlist = NULL;
    membercount = 0;
    g_free(member_owner);
    member_owner = NULL;
}

void BuildGCMemberList(MemberSet* set, char* owner_id) {
    ResetGCMemberList();
    member_owner = dup0(owner_id);

    membercount = set->n;
    memberhdr = gtk_label_new(NULL);
    UpdateMemberHeader();
    gtk_widget_add_css_class(memberhdr, "channel-category");
    gtk_widget_set_halign(memberhdr, GTK_ALIGN_START);
    gtk_widget_set_margin_start(memberhdr, 16);
    gtk_widget_set_margin_top(memberhdr, 12);
    gtk_widget_set_margin_bottom(memberhdr, 4);
    gtk_box_append(GTK_BOX(suvbox), memberhdr);

    GtkWidget* list = gtk_list_box_new();
    memberlist = list;
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);

    for (int i = 0; i < set->n; i++)
        gtk_list_box_append(GTK_LIST_BOX(list), BuildMemberRow(set->users[i]));

    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), list);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(suvbox), scroller);
}