/* F-19 English/Russian localization.
 * f19_translate is registered with gfx_setStringTranslator at F-19 startup so
 * every draw/measure path sees localized text.  Language: F19_LANG=ru (else en).
 */
#ifndef F19LANG_H
#define F19LANG_H

int f19_language(void);                      /* 0 = english, 1 = russian */
const char *f19_translate(const char *s);    /* draw-time EN->RU map; en mode = identity */
void f19_langInit(void);                     /* installs the translator hook */
int f19_textChars(const char *s);            /* codepoint count (UTF-8); == strlen for ASCII */

#endif
