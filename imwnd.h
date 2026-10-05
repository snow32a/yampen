#include <gtk/gtk.h>
#include "protocol/yamp.h"
#include <cjson/cJSON.h>
void StartMainIMWindow(YampUser usr, YampSpace* spaces, int nspaces, YampUser* incfq,
					   int fqcount, YampUser* outfq, int outfqcount);
void CreateMainIMWindow(GtkApplication *app);
