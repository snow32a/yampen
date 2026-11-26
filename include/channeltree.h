#pragma once
#include <gtk/gtk.h>
#include <yamp.h>

extern GtkWidget* channeltree_root;							   // the box inside the sidebar scroller
extern char* channeltree_space; // space that tree was built for

GtkWidget* BuildChannelEntry(YampChannel* ch, int depth);
void BuildChannelTree(GtkWidget* parent_box, YampChannel** channels,
							 int nchannels, int depth);
void OnChannelSidebarRightClick(GtkGestureClick* gesture, int n_press,
									   double x, double y, gpointer user_data);

typedef struct {
	char* space;
	char* category;
} ChannelMenuCtx;
typedef struct {
	ChannelMenuCtx menuctx;
	GtkWidget* nameentry;
} ChannelCreateBtnCtx;
int DeleteChannelEntry(GtkWidget* parent_box, char* channel_id);
int InsertConvIntoList(YampChannel* conv);
int ModifyConvInList(YampChannel* ch);
int DeleteConvFromList(char* id);