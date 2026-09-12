#define MARKDOWN_REGULAR 0
#define MARKDOWN_ITALIC 1
#define MARKDOWN_BOLD 2
#define MARKDOWN_UNDERLINE 3
#define MARKDOWN_IMAGE 4
#define MARKDOWN_LINK 5
typedef struct RMarkdownElement {
	unsigned char type;
	char *content;
	char *alt;
	struct RMarkdownElement **children;
	unsigned int nChildren;
} MarkdownElement;
MarkdownElement* ParseMarkdownStr(char* str);
void FreeMarkdownTree(MarkdownElement* tree);