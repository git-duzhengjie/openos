#include <stdio.h>
#include <string.h>

#include "../../src/user/browser_engine.h"

int main(void)
{
    const char *html = "<html><head><title>OpenOS &amp; Browser</title></head>"
                       "<body><h1>Hello</h1><p>Lightweight engine</p></body></html>";
    ob_html_tokenizer_base_t tokenizer;
    ob_html_token_t tok;
    ob_html_parser_base_t parser;
    ob_dom_document_t doc;
    int saw_html = 0;
    int saw_text = 0;

    ob_html_tokenizer_base_init(&tokenizer, html);
    while (tokenizer.iface.next(&tokenizer.iface, &tok) == 0 && tok.type != OB_HTML_TOKEN_EOF) {
        if (tok.type == OB_HTML_TOKEN_TAG && strcmp(tok.name, "html") == 0) saw_html = 1;
        if (tok.type == OB_HTML_TOKEN_TEXT && tok.text_len > 0) saw_text = 1;
    }
    if (!saw_html || !saw_text) {
        fprintf(stderr, "browser tokenizer smoke failed\n");
        return 1;
    }

    ob_html_parser_base_init(&parser);
    if (parser.iface.parse(&parser.iface, html, &doc) <= 1) {
        fprintf(stderr, "browser parser smoke failed\n");
        return 1;
    }
    if (doc.root != 0 || doc.nodes[0].type != OB_DOM_NODE_DOCUMENT) {
        fprintf(stderr, "browser DOM root smoke failed\n");
        return 1;
    }

    printf("browser engine smoke ok: nodes=%d\n", doc.count);
    return 0;
}
