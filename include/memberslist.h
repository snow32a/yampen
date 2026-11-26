#pragma once
#include <yamp.h>
#include <gtk/gtk.h>
typedef struct {
	YampUser** users;
	char* owner;
	int n;
} MemberSet;
MemberSet* MemberSetCopy(YampUser** src, int n);
void MemberSetFree(gpointer data);
void BuildGCMemberList(MemberSet* set, char* owner_id);
int AppendMember(const YampUser* u, const char* owner_id);
int RemoveMember(const char* user_id);

typedef void (*MemberActionCb)(YampUser* user, void* userdata);

void SetMemberListActionCallbacks(MemberActionCb kick, MemberActionCb transfer,
							  void* userdata);
int SetMemberOwner(const char* new_owner_id);
const char* GetMemberOwner(void);
int IsLocalUserOwner(void);
int UpdateMember(const YampUser* u);
int GetMemberCount(void);
void ResetGCMemberList(void);