#define MARKDOWN_TEXT (1<<0)
#define MARKDOWN_ITALIC (1<<1)
#define MARKDOWN_BOLD (1<<2)
#define MARKDOWN_UNDERLINE (1<<3)
#define MARKDOWN_IMAGE (1<<4)
#define MARKDOWN_LINK (1<<5)
#define MARKDOWN_QUOTE (1<<6)
#define MARKDOWN_CODE (1<<7)
#define MARKDOWN_CODEBLOCK (1<<8)
#define MARKDOWN_SUBTEXT (1<<9)
#define MARKDOWN_STRIKE (1<<10)
#define MARKDOWN_H1 (1<<11)
#define MARKDOWN_H2 (1<<12)
#define MARKDOWN_H3 (1<<13)
#define MARKDOWN_EMOJI (1<<14)
#define MARKDOWN_SPOILER (1<<15)
typedef struct RMarkdownElement {
	unsigned long long type;
	char *content;
	char *alt;
	struct RMarkdownElement **children;
	unsigned int nChildren;
} MarkdownElement;
MarkdownElement* ParseMarkdownStr(char* str);
void FreeMarkdownTree(MarkdownElement* tree);