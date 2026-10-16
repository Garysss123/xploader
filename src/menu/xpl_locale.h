/* SPDX-License-Identifier: MIT. UTF-8 catalogs with English fallback. */
#ifndef XPL_LOCALE_H
#define XPL_LOCALE_H
#include <stddef.h>
#include <stdint.h>
#define XPL_CJK_EDGE 24u
typedef struct {uint32_t codepoint;uint32_t row[XPL_CJK_EDGE];} xpl_cjk_glyph;
int xpl_locale_valid(const char *);
const char *xpl_locale_canonical(const char *);
const char *xpl_locale_message(const char *locale,const char *message_id);
const char *xpl_locale_translate(const char *locale,const char *english);
const xpl_cjk_glyph *xpl_locale_glyph(uint32_t codepoint);
/* Invalid/truncated UTF-8 consumes one byte and returns '?'. */
uint32_t xpl_utf8_next(const char *,size_t,size_t *consumed);
#endif
