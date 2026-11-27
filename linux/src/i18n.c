/* i18n.c — textes de l'interface en 6 langues (table générée : strings_gen.h). */
#include <stdarg.h>
#include <string.h>

#include "app.h"
#include "strings_gen.h"

static int g_lang = 1;
static const char *const CODES[6] = { "fr", "en", "es", "de", "it", "pt" };

void i18n_init(const char *lang)
{
    if (lang && *lang) {
        for (int i = 0; i < 6; i++)
            if (!strcmp(lang, CODES[i])) { g_lang = i; return; }
    }
    /* langue du système (LANGUAGE, LC_ALL, LC_MESSAGES, LANG) ; anglais si ce n'est pas l'une des six */
    g_lang = 1;
    const char *const *names = g_get_language_names();
    for (int k = 0; names && names[k]; k++) {
        if (!strcmp(names[k], "C") || !strcmp(names[k], "POSIX")) continue;
        for (int i = 0; i < 6; i++)
            if (!strncmp(names[k], CODES[i], 2) && (names[k][2] == 0 || names[k][2] == '_' || names[k][2] == '.' || names[k][2] == '@')) {
                g_lang = i;
                return;
            }
        break;   /* première langue préférée seulement */
    }
}

int i18n_lang(void) { return g_lang; }

const char *T(int id)
{
    if (id < 0 || id >= S_COUNT) return "";
    return STRINGS[id][g_lang];
}

char *TF(int id, ...)
{
    va_list ap;
    va_start(ap, id);
    char *s = g_strdup_vprintf(T(id), ap);
    va_end(ap);
    return s;
}

const char *TP(int id, int n)
{
    if (id < 0 || id >= P_COUNT) return "";
    int one = g_lang == 0 || g_lang == 5 ? (n <= 1) : (n == 1);   /* fr, pt : 0 et 1 au singulier */
    return PLURALS_T[id][g_lang][one ? 0 : 1];
}

const char *const *TA(int id)
{
    return id >= 0 && id < A_COUNT ? ARRAYS_T[id][g_lang] : NULL;
}

const char *const *TV(int id)
{
    return id >= 0 && id < A_COUNT ? VALUES_T[id] : NULL;
}
