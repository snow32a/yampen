#define MARKDOWN_TEXT 0
#define MARKDOWN_ITALIC 1
#define MARKDOWN_BOLD 2
#define MARKDOWN_UNDERLINE 3
#define MARKDOWN_IMAGE 4
#define MARKDOWN_LINK 5
#define MARKDOWN_QUOTE 6
#define MARKDOWN_CODE 7
#define MARKDOWN_CODEBLOCK 8
#define MARKDOWN_SUBTEXT 9
#define MARKDOWN_STRIKE 10
#define MARKDOWN_H1 101
#define MARKDOWN_H2 102
#define MARKDOWN_H3 103
#define MARKDOWN_EMOJI 200
#define MARKDOWN_SPOILER 300
typedef struct RMarkdownElement {
	unsigned char type;
	char *content;
	char *alt;
	struct RMarkdownElement **children;
	unsigned int nChildren;
} MarkdownElement;
MarkdownElement* ParseMarkdownStr(char* str);
void FreeMarkdownTree(MarkdownElement* tree);