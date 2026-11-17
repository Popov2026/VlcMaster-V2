/*
 * lang.h — traduction de l'interface (6 langues).
 *
 * Les textes sont écrits en français dans le code et passent par TR() :
 * TR(L"Pause") renvoie la traduction dans la langue choisie (Paramètres ›
 * Interface › Langue), ou le texte français s'il n'y en a pas.  La table
 * (lang_table.h) associe chaque texte français à ses traductions ; les chaînes
 * de format gardent les mêmes %ls / %d dans le même ordre.
 */
enum { L_FR, L_EN, L_ES, L_DE, L_IT, L_PT, L_COUNT };
static const wchar_t *LANG_NAMES[L_COUNT] = {
    L"Français", L"English", L"Español", L"Deutsch", L"Italiano", L"Português"
};
static int g_lang;               /* langue effective (choisie au démarrage) */

typedef struct { const wchar_t *t[L_COUNT]; } TrRow;
static const TrRow TRT[] = {
#include "lang_table.h"
    { { NULL } }
};

/* Recherche avec un petit cache par adresse (les littéraux ont une adresse fixe). */
static const wchar_t *TR(const wchar_t *fr)
{
    if (!fr || g_lang == L_FR) return fr;
    enum { NC = 1024 };
    static const wchar_t *ck[NC], *cv[NC];
    size_t hsh = ((size_t)fr >> 3) & (NC - 1);
    for (int k = 0; k < 8; k++) {
        size_t i = (hsh + (size_t)k) & (NC - 1);
        if (ck[i] == fr) return cv[i];
        if (!ck[i]) {
            const wchar_t *r = fr;
            for (const TrRow *row = TRT; row->t[0]; row++)
                if (!wcscmp(row->t[0], fr)) { if (row->t[g_lang]) r = row->t[g_lang]; break; }
            ck[i] = fr; cv[i] = r;
            return r;
        }
    }
    for (const TrRow *row = TRT; row->t[0]; row++)
        if (!wcscmp(row->t[0], fr)) return row->t[g_lang] ? row->t[g_lang] : fr;
    return fr;
}

/* Langue par défaut : celle de Windows si elle fait partie des six, sinon anglais. */
static int LangFromSystem(void)
{
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_FRENCH:     return L_FR;
    case LANG_SPANISH:    return L_ES;
    case LANG_GERMAN:     return L_DE;
    case LANG_ITALIAN:    return L_IT;
    case LANG_PORTUGUESE: return L_PT;
    default:              return L_EN;
    }
}
