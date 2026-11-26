#include <yamp.h>
char* JoinUsernames(YampUser** people, int n) {
	size_t len = 1;
	for (int i = 0; i < n; i++) {
		const char* nm = people[i]->displayname ? people[i]->displayname
												: people[i]->username;
		len += strlen(nm) + (i ? 2 : 0);
	}

	char* out = malloc(len);
	if (!out)
		return NULL;

	char* p = out;
	for (int i = 0; i < n; i++) {
		const char* nm = people[i]->displayname ? people[i]->displayname
												: people[i]->username;
		if (i) {
			*p++ = ',';
			*p++ = ' ';
		}
		size_t l = strlen(nm);
		memcpy(p, nm, l);
		p += l;
	}
	*p = '\0';
	return out;
}
