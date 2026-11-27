/*
 * json.h — petit lecteur JSON (arbre en mémoire), sans dépendance.
 *
 * Suffisant pour discover.json, lineup_status.json, le guide de
 * api.hdhomerun.com et l'API des releases GitHub.  Les chaînes sont
 * décodées en UTF-8 (\uXXXX et paires de substitution compris).
 */
#ifndef HR_JSON_H
#define HR_JSON_H

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal {
    JType t;
    double n;            /* J_NUM, J_BOOL (0/1) */
    char *s;             /* J_STR */
    int cnt;             /* J_ARR, J_OBJ : nombre d'éléments */
    struct JVal *v;      /* éléments */
    char **k;            /* J_OBJ : clés */
} JVal;

/* Analyse un texte JSON ; NULL si invalide.  À libérer avec json_free(). */
JVal *json_parse(const char *text);
void json_free(JVal *v);

/* Membre d'un objet (NULL si absent ou si o n'est pas un objet). */
const JVal *json_get(const JVal *o, const char *key);
/* Élément i d'un tableau (NULL si hors limites). */
const JVal *json_at(const JVal *a, int i);
/* Chaîne d'un membre (NULL si absent ou pas une chaîne). */
const char *json_str(const JVal *o, const char *key);
/* Nombre d'un membre (def si absent ; une chaîne numérique est acceptée). */
double json_num(const JVal *o, const char *key, double def);

#endif
