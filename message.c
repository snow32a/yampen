#include "message.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static void AddChild(MarkdownElement *root, MarkdownElement *child) {
	root->nChildren++;
	root->children =
		realloc(root->children, root->nChildren * sizeof(MarkdownElement *));
	root->children[root->nChildren - 1] = child;
}

static void FlushText(MarkdownElement *root, char **text) {
	if (!*text)
		return;

	MarkdownElement *node = malloc(sizeof(MarkdownElement));
	node->type = 0;
	node->alt = NULL;
	node->content = *text;
	node->nChildren = 0;
	node->children = NULL;

	AddChild(root, node);
	*text = NULL;
}

static MarkdownElement *MakeTextNode(const char *str, size_t len) {
	MarkdownElement *node = malloc(sizeof(MarkdownElement));
	node->type = 0;
	node->alt = NULL;
	node->content = malloc(len + 1);
	memcpy(node->content, str, len);
	node->content[len] = '\0';
	node->nChildren = 0;
	node->children = NULL;
	return node;
}

MarkdownElement *ParseMarkdownStr(char *str) {
	MarkdownElement *root = malloc(sizeof(MarkdownElement));
	root->type = 0;
	root->alt = NULL;
	root->content = NULL;
	root->nChildren = 0;
	root->children = NULL;

	char *text = NULL;

	for (int i = 0; str[i]; i++) {
		// automatic link detection: http:// or https://
		if (strncmp(str + i, "http://", 7) == 0 ||
			strncmp(str + i, "https://", 8) == 0) {
			int j = i;
			while (str[j] && !isspace((unsigned char)str[j]))
				j++;

			size_t len = j - i;
			char *url = malloc(len + 1);
			memcpy(url, str + i, len);
			url[len] = '\0';

			MarkdownElement *tag = malloc(sizeof(MarkdownElement));
			tag->type = MARKDOWN_LINK;
			tag->alt = NULL;
			tag->content = url;
			tag->nChildren = 0;
			tag->children = NULL;
			AddChild(tag, MakeTextNode(url, len));

			FlushText(root, &text);
			AddChild(root, tag);

			i = j - 1;
			continue;
		}

		switch (str[i]) {
		case '_': {
			break;
		}
		case '[': {
			char *closeBracket = strchr(str + i + 1, ']');
			if (!closeBracket || closeBracket[1] != '(')
				break;

			char *closeParen = strchr(closeBracket + 2, ')');
			if (!closeParen)
				break;

			size_t labelLen = closeBracket - (str + i + 1);
			char *label = malloc(labelLen + 1);
			memcpy(label, str + i + 1, labelLen);
			label[labelLen] = '\0';

			size_t urlLen = closeParen - (closeBracket + 2);
			char *url = malloc(urlLen + 1);
			memcpy(url, closeBracket + 2, urlLen);
			url[urlLen] = '\0';

			MarkdownElement *tag = ParseMarkdownStr(label);
			free(label);
			tag->type = MARKDOWN_LINK;
			tag->content = url;

			FlushText(root, &text);
			AddChild(root, tag);

			i = closeParen - str;
			break;
		}
		case '*': {
			if (!str[i + 1]) {
				break;
			}
			if (str[i + 1] == '*' && str[i + 2] == '*') {
				char *end = strstr(str + i + 3, "***");
				if (!end)
					break;

				size_t len = end - (str + i + 3);

				char cont[len + 1];
				memcpy(cont, str + i + 3, len);
				cont[len] = '\0';

				MarkdownElement *bold = ParseMarkdownStr(cont);
				bold->type = MARKDOWN_BOLD;

				MarkdownElement *italic = malloc(sizeof(MarkdownElement));
				italic->type = MARKDOWN_ITALIC;
				italic->alt = NULL;
				italic->content = NULL;
				italic->nChildren = 0;
				italic->children = NULL;
				AddChild(italic, bold);

				FlushText(root, &text);
				AddChild(root, italic);

				i = (end - str) + 2;
			} else if (str[i + 1] == '*') {
				char *end = strstr(str + i + 2, "**");

				if (!end)
					break;

				size_t len = end - (str + i + 2);

				char cont[len + 1];
				memcpy(cont, str + i + 2, len);
				cont[len] = '\0';

				MarkdownElement *tag = ParseMarkdownStr(cont);
				tag->type = MARKDOWN_BOLD;

				FlushText(root, &text);
				AddChild(root, tag);

				i = (end - str) + 1;
			} else {
				// Italic !!!
				char *end = strstr(str + i + 1, "*");

				if (!end)
					break;

				size_t len = end - (str + i + 1);

				char cont[len + 1];
				memcpy(cont, str + i + 1, len);
				cont[len] = '\0';

				MarkdownElement *tag = ParseMarkdownStr(cont);
				tag->type = MARKDOWN_ITALIC;

				FlushText(root, &text);
				AddChild(root, tag);

				i = (end - str);
			}
			break;
		}
		default:
			if (!text) {
				text = malloc(2);
				text[0] = str[i];
				text[1] = '\0';
			} else {
				int len = strlen(text);
				text = realloc(text, len + 2);
				text[len] = str[i];
				text[len + 1] = '\0';
			}
		}
	}

	FlushText(root, &text);

	return root;
}

void FreeMarkdownTree(MarkdownElement *tree) {
	for (int i = 0; i < tree->nChildren; i++) {
		FreeMarkdownTree(tree->children[i]);
	}
	free(tree->children);
	free(tree->alt);
	free(tree->content);
	free(tree);
}