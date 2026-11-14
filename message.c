#include "message.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { MD_BLOCK = 1, MD_NOLINK = 2 };
#define MD_MAXDEPTH 24
#define NPOS ((size_t)-1)

static void ParseInline(const char* s, size_t len, MarkdownElement* parent,
						int flags, int depth);


static char* Dup(const char* s, size_t len) {
	char* r = malloc(len + 1);
	if (len)
		memcpy(r, s, len);
	r[len] = '\0';
	return r;
}

static MarkdownElement* NewNode(int type) {
	MarkdownElement* n = calloc(1, sizeof *n);
	n->type = type;
	return n;
}

static MarkdownElement* MakeLeaf(int type, const char* s, size_t len) {
	MarkdownElement* n = NewNode(type);
	n->content = Dup(s, len);
	return n;
}

static void AddChild(MarkdownElement* parent, MarkdownElement* child) {
	parent->children = realloc(
		parent->children, (parent->nChildren + 1) * sizeof(MarkdownElement*));
	parent->children[parent->nChildren++] = child;
}


// an appendable buffer thingy in order to not dirty rest of the stuff

typedef struct {
	char* data;
	size_t len, cap;
} Buf;

static void BufPut(Buf* b, const char* p, size_t n) {
	if (!n)
		return;
	if (b->len + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 32;
		while (cap < b->len + n + 1)
			cap *= 2;
		b->data = realloc(b->data, cap);
		b->cap = cap;
	}
	memcpy(b->data + b->len, p, n);
	b->len += n;
	b->data[b->len] = '\0';
}

static void FlushText(MarkdownElement* parent, Buf* b) {
	if (!b->len)
		return;
	MarkdownElement* node = NewNode(MARKDOWN_TEXT);
	node->content = b->data; // ownership is now the nodes
	AddChild(parent, node);
	b->data = NULL;
	b->len = b->cap = 0;
}

static size_t LineEnd(const char* s, size_t len, size_t from) {
	while (from < len && s[from] != '\n')
		from++;
	return from;
}

static size_t UrlScheme(const char* s, size_t len) {
	if (len > 7 && memcmp(s, "http://", 7) == 0)
		return 7;
	if (len > 8 && memcmp(s, "https://", 8) == 0)
		return 8;
	return 0;
}

// EXTERNAL CODE

// unicode emoji detection

typedef struct {
	uint32_t lo, hi;
} Range;

// emoji ranges that render as one alone
static const Range kEmojiPres[] = {
	{0x231A, 0x231B},   {0x23E9, 0x23EC},   {0x23F0, 0x23F0},
	{0x23F3, 0x23F3},   {0x25FD, 0x25FE},   {0x2614, 0x2615},
	{0x2648, 0x2653},   {0x267F, 0x267F},   {0x2693, 0x2693},
	{0x26A1, 0x26A1},   {0x26AA, 0x26AB},   {0x26BD, 0x26BE},
	{0x26C4, 0x26C5},   {0x26CE, 0x26CE},   {0x26D4, 0x26D4},
	{0x26EA, 0x26EA},   {0x26F2, 0x26F3},   {0x26F5, 0x26F5},
	{0x26FA, 0x26FA},   {0x26FD, 0x26FD},   {0x2705, 0x2705},
	{0x270A, 0x270B},   {0x2728, 0x2728},   {0x274C, 0x274C},
	{0x274E, 0x274E},   {0x2753, 0x2755},   {0x2757, 0x2757},
	{0x2795, 0x2797},   {0x27B0, 0x27B0},   {0x27BF, 0x27BF},
	{0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},   {0x2B55, 0x2B55},
	{0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E},
	{0x1F191, 0x1F19A}, {0x1F201, 0x1F201}, {0x1F21A, 0x1F21A},
	{0x1F22F, 0x1F22F}, {0x1F232, 0x1F23A}, {0x1F250, 0x1F251},
	{0x1F300, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F7E0, 0x1F7F0},
	{0x1F90C, 0x1F9FF}, {0x1FA70, 0x1FAFF},
};

// emojis that become emojis if they have a specific content
static const Range kEmojiText[] = {
	{0x00A9, 0x00A9},   {0x00AE, 0x00AE},   {0x203C, 0x203C},
	{0x2049, 0x2049},   {0x2122, 0x2122},   {0x2139, 0x2139},
	{0x2194, 0x21AA},   {0x231A, 0x23FF},   {0x24C2, 0x24C2},
	{0x25AA, 0x25FE},   {0x2600, 0x27BF},   {0x2934, 0x2935},
	{0x2B05, 0x2B55},   {0x3030, 0x3030},   {0x303D, 0x303D},
	{0x3297, 0x3297},   {0x3299, 0x3299},   {0x1F170, 0x1F17F},
	{0x1F202, 0x1F202}, {0x1F237, 0x1F237},
};

static int InRanges(uint32_t cp, const Range* r, size_t n) {
	for (size_t i = 0; i < n; i++)
		if (cp >= r[i].lo && cp <= r[i].hi)
			return 1;
	return 0;
}

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static int Utf8Decode(const unsigned char* s, size_t len, uint32_t* cp) {
	if (!len)
		return 0;
	unsigned char c = s[0];
	if (c < 0x80) {
		*cp = c;
		return 1;
	}
	int n;
	uint32_t v;
	if ((c & 0xE0) == 0xC0) {
		n = 2;
		v = c & 0x1F;
	} else if ((c & 0xF0) == 0xE0) {
		n = 3;
		v = c & 0x0F;
	} else if ((c & 0xF8) == 0xF0) {
		n = 4;
		v = c & 0x07;
	} else {
		return 0;
	}
	if ((size_t)n > len)
		return 0;
	for (int k = 1; k < n; k++) {
		if ((s[k] & 0xC0) != 0x80)
			return 0;
		v = (v << 6) | (s[k] & 0x3F);
	}
	*cp = v;
	return n;
}

static int IsRegional(uint32_t cp) {
	return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

static int IsEmojiBase(uint32_t cp, int hasVS, int afterZwj) {
	if (InRanges(cp, kEmojiPres, COUNT(kEmojiPres)))
		return 1;
	return (hasVS || afterZwj) && InRanges(cp, kEmojiText, COUNT(kEmojiText));
}

size_t MatchEmoji(const unsigned char* s, size_t len) {
	if (!len)
		return 0;
	if (s[0] < 0x80 && !isdigit(s[0]) && s[0] != '#' && s[0] != '*')
		return 0;

	uint32_t cp, nx;
	int n = Utf8Decode(s, len, &cp);
	if (!n)
		return 0;

	if (cp < 0x80) { // keycaps
		size_t pos = n;
		int m = Utf8Decode(s + pos, len - pos, &nx);
		if (m && nx == 0xFE0F) {
			pos += m;
			m = Utf8Decode(s + pos, len - pos, &nx);
		}
		if (m && nx == 0x20E3)
			return pos + m;
		return 0;
	}

	if (IsRegional(cp)) { // flag indicator
		int m = Utf8Decode(s + n, len - n, &nx);
		return (m && IsRegional(nx)) ? (size_t)(n + m) : 0;
	}

	size_t pos = 0;
	int afterZwj = 0;
	for (;;) {
		n = Utf8Decode(s + pos, len - pos, &cp);
		if (!n)
			break;
		int m = Utf8Decode(s + pos + n, len - pos - n, &nx);
		int vs = m && nx == 0xFE0F;
		if (!IsEmojiBase(cp, vs, afterZwj))
			break;
		pos += n + (vs ? m : 0);

		// skin tone modifier
		n = Utf8Decode(s + pos, len - pos, &cp);
		if (n && cp >= 0x1F3FB && cp <= 0x1F3FF)
			pos += n;

		// tag sequence
		while ((n = Utf8Decode(s + pos, len - pos, &cp)) && cp >= 0xE0020 &&
			   cp <= 0xE007F) {
			pos += n;
			if (cp == 0xE007F)
				break;
		}

		// ZWJ continued by another emoji
		n = Utf8Decode(s + pos, len - pos, &cp);
		if (n && cp == 0x200D) {
			uint32_t b, c3;
			int bn = Utf8Decode(s + pos + n, len - pos - n, &b);
			if (bn) {
				int m3 = Utf8Decode(s + pos + n + bn, len - pos - n - bn, &c3);
				if (IsEmojiBase(b, m3 && c3 == 0xFE0F, 1)) {
					pos += n;
					afterZwj = 1;
					continue;
				}
			}
		}
		break;
	}
	return pos;
}

static size_t FindClose(const char* s, size_t len, size_t from, const char* d,
						size_t n) {
	char look = (d[0] == '*' || d[0] == '_') ? d[0] : 0;
	for (size_t j = from; j + n <= len;) {
		if (s[j] == '\\') {
			j += 2;
			continue;
		}
		if (memcmp(s + j, d, n) == 0 &&
			!(look && j + n < len && s[j + n] == look))
			return j;
		j++;
	}
	return NPOS;
}

static int TryWrap(MarkdownElement* parent, Buf* text, const char* s,
				   size_t len, size_t* pos, size_t n, int type, int inner,
				   int flags, int depth) {
	size_t i = *pos;
	size_t close = FindClose(s, len, i + n + 1, s + i, n);
	if (close == NPOS)
		return 0;

	FlushText(parent, text);
	MarkdownElement* node = NewNode(type);
	MarkdownElement* target = node;
	if (inner >= 0) {
		target = NewNode(inner);
		AddChild(node, target);
	}
	ParseInline(s + i + n, close - (i + n), target, flags & ~MD_BLOCK,
				depth + 1);
	AddChild(parent, node);
	*pos = close + n;
	return 1;
}

static int TryItalic(MarkdownElement* parent, Buf* text, const char* s,
					 size_t len, size_t* pos, char ch, int flags, int depth) {
	size_t i = *pos;
	if (i + 1 >= len || isspace((unsigned char)s[i + 1]))
		return 0;
	if (ch == '_' && i > 0 &&
		(isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_'))
		return 0; /* snake_case_names stay literal */

	size_t close = NPOS;
	for (size_t j = i + 1; j < len;) {
		if (s[j] == '\\') {
			j += 2;
			continue;
		}
		if (s[j] != ch) {
			j++;
			continue;
		}
		if (j + 1 < len && s[j + 1] == ch) {
			j += 2;
			continue;
		}
		if (j == i + 1)
			return 0;
		int bad = (ch == '*') ? isspace((unsigned char)s[j - 1])
							  : (j + 1 < len && isalnum((unsigned char)s[j + 1]));
		if (bad)
			return 0;
		close = j;
		break;
	}
	if (close == NPOS)
		return 0;

	FlushText(parent, text);
	MarkdownElement* node = NewNode(MARKDOWN_ITALIC);
	ParseInline(s + i + 1, close - i - 1, node, flags & ~MD_BLOCK, depth + 1);
	AddChild(parent, node);
	*pos = close + 1;
	return 1;
}

static int TryCodeBlock(MarkdownElement* parent, Buf* text, const char* s,
						size_t len, size_t* pos) {
	size_t i = *pos, start = i + 3, k = start;
	while (k + 3 <= len && memcmp(s + k, "```", 3) != 0)
		k++;
	if (k + 3 > len)
		return 0;

	size_t cs = start, langEnd = start;
	while (langEnd < k && (isalnum((unsigned char)s[langEnd]) ||
						   strchr("_+-.#", s[langEnd])))
		langEnd++;
	int hasLang = langEnd > start && langEnd < k && s[langEnd] == '\n';
	if (hasLang)
		cs = langEnd + 1;

	while (cs < k && s[cs] == '\n')
		cs++;
	size_t ce = k;
	while (ce > cs && s[ce - 1] == '\n')
		ce--;
	if (ce == cs)
		return 0;

	FlushText(parent, text);
	MarkdownElement* node = MakeLeaf(MARKDOWN_CODEBLOCK, s + cs, ce - cs);
	if (hasLang)
		node->alt = Dup(s + start, langEnd - start);
	AddChild(parent, node);
	*pos = k + 3;
	return 1;
}

static int TryInlineCode(MarkdownElement* parent, Buf* text, const char* s,
						 size_t len, size_t* pos) {
	size_t i = *pos, n = 0;
	while (i + n < len && s[i + n] == '`')
		n++;

	for (size_t p = i + n; p < len;) {
		if (s[p] != '`') {
			p++;
			continue;
		}
		size_t r = 0;
		while (p + r < len && s[p + r] == '`')
			r++;
		if (r == n) {
			FlushText(parent, text);
			AddChild(parent, MakeLeaf(MARKDOWN_CODE, s + i + n, p - (i + n)));
			*pos = p + n;
			return 1;
		}
		p += r;
	}
	// it was NOT inline code
	BufPut(text, s + i, n);
	*pos = i + n;
	return 1;
}

static int TryAutoLink(MarkdownElement* parent, Buf* text, const char* s,
					   size_t len, size_t* pos) {
	size_t i = *pos, sch = UrlScheme(s + i, len - i);
	if (!sch)
		return 0;

	size_t j = i + sch;
	while (j < len && !isspace((unsigned char)s[j]) && s[j] != '<')
		j++;

	for (; j > i + sch; j--) {
		char t = s[j - 1];
		if (t && strchr(".,:;!?\"'*_~|]", t))
			continue;
		if (t == ')') {
			int open = 0, close = 0;
			for (size_t k = i + sch; k < j; k++) {
				open += s[k] == '(';
				close += s[k] == ')';
			}
			if (close > open)
				continue;
		}
		break;
	}
	if (j <= i + sch)
		return 0;

	FlushText(parent, text);
	MarkdownElement* node = MakeLeaf(MARKDOWN_LINK, s + i, j - i);
	AddChild(node, MakeLeaf(MARKDOWN_TEXT, s + i, j - i));
	AddChild(parent, node);
	*pos = j;
	return 1;
}

static int TryAngleLink(MarkdownElement* parent, Buf* text, const char* s,
						size_t len, size_t* pos) {
	size_t i = *pos;
	if (!UrlScheme(s + i + 1, len - i - 1))
		return 0;
	size_t p = i + 1;
	while (p < len && s[p] != '>' && !isspace((unsigned char)s[p]))
		p++;
	if (p >= len || s[p] != '>')
		return 0;

	FlushText(parent, text);
	MarkdownElement* node = MakeLeaf(MARKDOWN_LINK, s + i + 1, p - i - 1);
	AddChild(node, MakeLeaf(MARKDOWN_TEXT, s + i + 1, p - i - 1));
	AddChild(parent, node);
	*pos = p + 1;
	return 1;
}

static int TryMaskedLink(MarkdownElement* parent, Buf* text, const char* s,
						 size_t len, size_t* pos, int flags, int depth) {
	size_t i = *pos, j = i + 1;
	int d = 1;
	while (j < len) {
		if (s[j] == '\\') {
			j += 2;
			continue;
		}
		if (s[j] == '[')
			d++;
		else if (s[j] == ']' && --d == 0)
			break;
		j++;
	}
	if (j >= len || j == i + 1 || j + 1 >= len || s[j + 1] != '(')
		return 0;

	size_t p = j + 2, us, ue;
	while (p < len && s[p] == ' ')
		p++;
	if (p < len && s[p] == '<') {
		us = ++p;
		while (p < len && s[p] != '>' && !isspace((unsigned char)s[p]))
			p++;
		if (p >= len || s[p] != '>')
			return 0;
		ue = p++;
	} else {
		us = p;
		int dp = 0;
		while (p < len && !isspace((unsigned char)s[p])) {
			if (s[p] == '(')
				dp++;
			else if (s[p] == ')') {
				if (!dp)
					break;
				dp--;
			}
			p++;
		}
		ue = p;
	}
	while (p < len && s[p] == ' ')
		p++;
	if (p < len && (s[p] == '"' || s[p] == '\'')) {
		char q = s[p++];
		while (p < len && s[p] != q)
			p++;
		if (p >= len)
			return 0;
		p++;
		while (p < len && s[p] == ' ')
			p++;
	}
	if (p >= len || s[p] != ')' || !UrlScheme(s + us, ue - us))
		return 0;

	FlushText(parent, text);
	MarkdownElement* node = MakeLeaf(MARKDOWN_LINK, s + us, ue - us);
	ParseInline(s + i + 1, j - i - 1, node, (flags & ~MD_BLOCK) | MD_NOLINK,
				depth + 1);
	AddChild(parent, node);
	*pos = p + 1;
	return 1;
}

static int TryBlock(MarkdownElement* parent, Buf* text, const char* s,
					size_t len, size_t* pos, int flags, int depth) {
	size_t i = *pos;
	int sub = flags & MD_NOLINK;

	if (len - i >= 4 && memcmp(s + i, ">>> ", 4) == 0) {
		FlushText(parent, text);
		MarkdownElement* q = NewNode(MARKDOWN_QUOTE);
		ParseInline(s + i + 4, len - i - 4, q, flags, depth + 1);
		AddChild(parent, q);
		*pos = len;
		return 1;
	}

	if (len - i >= 2 && s[i] == '>' && s[i + 1] == ' ') {
		Buf q = {0};
		size_t p = i;
		for (;;) {
			p += 2;
			size_t e = LineEnd(s, len, p);
			BufPut(&q, s + p, e - p);
			p = e < len ? e + 1 : e;
			if (len - p >= 2 && s[p] == '>' && s[p + 1] == ' ')
				BufPut(&q, "\n", 1);
			else
				break;
		}
		FlushText(parent, text);
		MarkdownElement* node = NewNode(MARKDOWN_QUOTE);
		ParseInline(q.data ? q.data : "", q.len, node, flags, depth + 1);
		AddChild(parent, node);
		free(q.data);
		*pos = p;
		return 1;
	}

	int type = 0;
	size_t prefix = 0;
	if (s[i] == '#') {
		size_t h = 0;
		while (h < 3 && i + h < len && s[i + h] == '#')
			h++;
		if (i + h < len && s[i + h] == ' ') {
			type = MARKDOWN_H1 + (int)h - 1;
			prefix = h + 1;
		}
	} else if (len - i >= 3 && s[i] == '-' && s[i + 1] == '#' &&
			   s[i + 2] == ' ') {
		type = MARKDOWN_SUBTEXT;
		prefix = 3;
	}
	if (type) {
		size_t cs = i + prefix;
		while (cs < len && s[cs] == ' ')
			cs++;
		size_t e = LineEnd(s, len, cs);
		if (e > cs) {
			FlushText(parent, text);
			MarkdownElement* node = NewNode(type);
			ParseInline(s + cs, e - cs, node, sub, depth + 1);
			AddChild(parent, node);
			*pos = e < len ? e + 1 : e; // do not bring in the \n
			return 1;
		}
	}
	return 0;
}

static void ParseInline(const char* s, size_t len, MarkdownElement* parent,
						int flags, int depth) {
	Buf text = {0};
	int nest = depth < MD_MAXDEPTH;
	size_t i = 0;

	while (i < len) {
		unsigned char c = (unsigned char)s[i];
		size_t rest = len - i;

		if (c == '\\' && rest > 1) {
			unsigned char nx = (unsigned char)s[i + 1];
			if (!isalnum(nx) && !isspace(nx)) {
				BufPut(&text, s + i + 1, 1);
				i += 2;
				continue;
			}
		}

		size_t en = MatchEmoji((const unsigned char*)s + i, rest);
		if (en) {
			FlushText(parent, &text);
			AddChild(parent, MakeLeaf(MARKDOWN_EMOJI, s + i, en));
			i += en;
			continue;
		}

		if (nest && (flags & MD_BLOCK) && (i == 0 || s[i - 1] == '\n') &&
			TryBlock(parent, &text, s, len, &i, flags, depth))
			continue;

		if (c == '`') {
			if (rest >= 3 && memcmp(s + i, "```", 3) == 0 &&
				TryCodeBlock(parent, &text, s, len, &i))
				continue;
			TryInlineCode(parent, &text, s, len, &i);
			continue;
		}

		if (!(flags & MD_NOLINK)) {
			if (c == 'h' && TryAutoLink(parent, &text, s, len, &i))
				continue;
			if (c == '<' && TryAngleLink(parent, &text, s, len, &i))
				continue;
			if (nest && c == '[' &&
				TryMaskedLink(parent, &text, s, len, &i, flags, depth))
				continue;
		}

		if (nest) {
			int ok = 0;
			switch (c) {
			case '*':
				if (rest >= 3 && memcmp(s + i, "***", 3) == 0)
					ok = TryWrap(parent, &text, s, len, &i, 3, MARKDOWN_ITALIC,
								 MARKDOWN_BOLD, flags, depth);
				if (!ok && rest >= 2 && s[i + 1] == '*')
					ok = TryWrap(parent, &text, s, len, &i, 2, MARKDOWN_BOLD,
								 -1, flags, depth);
				if (!ok)
					ok = TryItalic(parent, &text, s, len, &i, '*', flags,
								   depth);
				break;
			case '_':
				if (rest >= 3 && memcmp(s + i, "___", 3) == 0)
					ok = TryWrap(parent, &text, s, len, &i, 3,
								 MARKDOWN_UNDERLINE, MARKDOWN_ITALIC, flags,
								 depth);
				if (!ok && rest >= 2 && s[i + 1] == '_')
					ok = TryWrap(parent, &text, s, len, &i, 2,
								 MARKDOWN_UNDERLINE, -1, flags, depth);
				if (!ok)
					ok = TryItalic(parent, &text, s, len, &i, '_', flags,
								   depth);
				break;
			case '~':
				if (rest >= 2 && s[i + 1] == '~')
					ok = TryWrap(parent, &text, s, len, &i, 2, MARKDOWN_STRIKE,
								 -1, flags, depth);
				break;
			case '|':
				if (rest >= 2 && s[i + 1] == '|')
					ok = TryWrap(parent, &text, s, len, &i, 2,
								 MARKDOWN_SPOILER, -1, flags, depth);
				break;
			}
			if (ok)
				continue;
		}

		BufPut(&text, s + i, 1);
		i++;
	}

	FlushText(parent, &text);
}

MarkdownElement* ParseMarkdownStr(char* str) {
	MarkdownElement* root = NewNode(MARKDOWN_TEXT);
	ParseInline(str, strlen(str), root, MD_BLOCK, 0);
	return root;
}

void FreeMarkdownTree(MarkdownElement* tree) {
	if (!tree)
		return;
	for (int i = 0; i < tree->nChildren; i++)
		FreeMarkdownTree(tree->children[i]);
	free(tree->children);
	free(tree->alt);
	free(tree->content);
	free(tree);
}