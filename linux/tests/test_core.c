/*
 * test_core.c — tests de core.c et json.c (sans GTK, sans réseau).
 *
 *   test_core [guide.json [lineup.m3u]]
 *
 * Les arguments facultatifs permettent de vérifier de vraies données
 * (guide de api.hdhomerun.com, playlist d'un tuner) en plus des cas intégrés.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "json.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "%s:%d: échec : %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKS(a, b) do { checks++; const char *x_ = (a), *y_ = (b); \
    if (!x_ || strcmp(x_, y_)) { fails++; fprintf(stderr, "%s:%d: « %s » au lieu de « %s »\n", __FILE__, __LINE__, x_ ? x_ : "(null)", y_); } } while (0)

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) b[n] = 0;
    fclose(f);
    return b;
}

static void test_json(void)
{
    JVal *v = json_parse(" {\"a\": [1, 2.5, -3e2], \"s\": \"\\u00e9t\\u00E9 \\ud83d\\ude00 \\\"q\\\"\", \"t\": true, \"n\": null, \"o\": {}, \"num\": \"42\"} ");
    CHECK(v != NULL);
    CHECK(json_get(v, "a")->cnt == 3);
    CHECK(json_at(json_get(v, "a"), 2)->n == -300);
    CHECKS(json_str(v, "s"), "\xc3\xa9t\xc3\xa9 \xf0\x9f\x98\x80 \"q\"");
    CHECK(json_num(v, "t", 0) == 1);
    CHECK(json_get(v, "n")->t == J_NULL);
    CHECK(json_get(v, "o")->t == J_OBJ && json_get(v, "o")->cnt == 0);
    CHECK(json_num(v, "num", 0) == 42);
    CHECK(json_num(v, "absent", 7) == 7);
    CHECK(json_str(v, "a") == NULL);
    json_free(v);
    CHECK(json_parse("{\"a\":1,}") == NULL);
    CHECK(json_parse("[1 2]") == NULL);
    CHECK(json_parse("{\"a\":\"x") == NULL);
    CHECK(json_parse("") == NULL);
    JVal *e = json_parse("[]");
    CHECK(e && e->t == J_ARR && e->cnt == 0);
    json_free(e);
}

static void test_playlist(void)
{
    const char *m3u =
        "\xEF\xBB\xBF#EXTM3U\r\n"
        "#EXTINF:-1 channel-id=\"1\" channel-number=\"1\" tvg-name=\"TF1\",1 TF1\r\n"
        "http://192.168.1.90:5004/auto/v1\r\n"
        "\r\n"
        "#EXTINF:-1 tvg-chno=\"5.1\",France 5\n"
        "http://h/auto/v5.1\n"
        "#EXTINF:-1,\n"
        "http://h/auto/v13\n"
        "http://h/stream/radio\n";
    ChannelList l;
    CHECK(playlist_parse(m3u, &l) == 4);
    CHECKS(l.v[0].num, "1");
    CHECKS(l.v[0].name, "TF1");
    CHECKS(l.v[0].url, "http://192.168.1.90:5004/auto/v1");
    CHECKS(l.v[1].num, "5.1");
    CHECKS(l.v[1].name, "France 5");
    CHECKS(l.v[2].num, "13");
    CHECKS(l.v[2].name, "v13");
    CHECKS(l.v[3].num, "");
    CHECKS(l.v[3].name, "radio");
    CHECK(playlist_find_num(&l, "5.1") == 1);
    CHECK(playlist_find_url(&l, "http://h/auto/v13") == 2);
    CHECK(playlist_find_url(&l, "x") == -1);
    playlist_free(&l);
}

static const char *PAGE1 =
    "[{\"GuideNumber\":\"1\",\"GuideName\":\"TF1\",\"Guide\":["
    "{\"StartTime\":1000,\"EndTime\":2000,\"Title\":\"Journal\",\"EpisodeTitle\":\"Ep\",\"EpisodeNumber\":\"S1E2\",\"Synopsis\":\"Infos\"},"
    "{\"StartTime\":2000,\"EndTime\":3000,\"Title\":\"Film\"}]},"
    "{\"GuideNumber\":\"2\",\"GuideName\":\"F2\",\"Guide\":[{\"StartTime\":1500,\"EndTime\":4000,\"Title\":\"Sport\",\"EpisodeNumber\":\"\"}]},"
    "{\"GuideName\":\"sans numéro\",\"Guide\":[]}]";
static const char *PAGE2 =
    "[{\"GuideNumber\":\"1\",\"GuideName\":\"TF1\",\"Guide\":["
    "{\"StartTime\":2000,\"EndTime\":3000,\"Title\":\"Film (doublon)\"},"
    "{\"StartTime\":3000,\"EndTime\":90000,\"Title\":\"Nuit\"}]}]";

typedef struct { int calls; int fail_guide; } FakeNet;

static char *fake_fetch(const char *url, int timeout_ms, char **err, void *ud)
{
    FakeNet *f = ud;
    (void)timeout_ms;
    f->calls++;
    if (strstr(url, "/discover.json")) {
        if (strstr(url, "notuner")) { *err = strdup("refusé"); return NULL; }
        return strdup("{\"DeviceAuth\":\"a b+c\",\"ModelNumber\":\"HDHR5-4DT\"}");
    }
    if (strstr(url, "guide.php")) {
        if (!strstr(url, "DeviceAuth=a%20b%2Bc")) { *err = strdup("auth mal encodée"); return NULL; }
        if (f->fail_guide) { *err = strdup("HTTP 403"); return NULL; }
        if (strstr(url, "&Start=4000")) return strdup(PAGE2);
        if (strstr(url, "&Start=")) return strdup("[]");
        return strdup(PAGE1);
    }
    *err = strdup("?");
    return NULL;
}

static void test_guide(void)
{
    FakeNet f = { 0, 0 };
    Guide g;
    char *err = NULL;
    int r = guide_load("http://t", fake_fetch, &f, 1000, &g, &err);
    CHECK(r == GUIDE_OK);
    CHECK(err == NULL);
    CHECK(g.n == 2);
    const GuideCh *c1 = guide_find(&g, "1");
    CHECK(c1 && c1->n == 3);
    CHECKS(c1->p[0].episode, "Ep (S1E2)");
    CHECKS(c1->p[0].synopsis, "Infos");
    CHECKS(c1->p[1].title, "Film");
    CHECKS(c1->p[2].title, "Nuit");
    CHECK(c1->p[1].episode == NULL);
    CHECK(g.max_end == 90000);
    CHECKS(guide_at(c1, 2500)->title, "Film");
    CHECK(guide_at(c1, 999) == NULL);
    CHECKS(guide_next(c1, 2500)->title, "Nuit");
    const GuideCh *c2 = guide_find(&g, "2");
    CHECK(c2 && c2->p[0].episode == NULL);
    CHECK(guide_find(&g, "9") == NULL);
    CHECK(f.calls == 3);   /* discover + 2 pages : la 2e atteint now + 24 h */
    guide_free(&g);

    f.calls = 0;
    r = guide_load("http://notuner", fake_fetch, &f, 1000, &g, &err);
    CHECK(r == GUIDE_ERR_NO_TUNER);
    CHECKS(err, "http://notuner");
    free(err);
    guide_free(&g);

    f.fail_guide = 1;
    r = guide_load("http://t", fake_fetch, &f, 1000, &g, &err);
    CHECK(r == GUIDE_ERR_SERVICE);
    CHECKS(err, "HTTP 403");
    free(err);
    guide_free(&g);
}

static void test_tracks(void)
{
    char k[128], b[64];
    track_key("Track 1 - [French]", k, sizeof k);
    CHECKS(k, "french");
    track_key("Piste 3 - [French] - audio description", k, sizeof k);
    CHECKS(k, "french|ad");
    track_key("Track 2 - [qaa]", k, sizeof k);
    CHECKS(k, "qaa");
    track_key("DVB subtitles - [French] Hard of hearing", k, sizeof k);
    CHECKS(k, "french|hoh");
    track_key("Stereo", k, sizeof k);
    CHECKS(k, "stereo");
    CHECK(track_bracket("Track 1 - [ English ]", b, sizeof b) && !strcmp(b, "English"));
    CHECK(!track_bracket("Track 1 - []", b, sizeof b));
    CHECK(track_is_ad("Audiodescription"));
    CHECK(!track_is_hoh("Track 1"));
}

static void test_misc(void)
{
    CHECK(version_cmp("v3.10", "3.9") > 0);
    CHECK(version_cmp("3.7", "v3.7.0") == 0);
    CHECK(version_cmp("3.6.2", "3.7") < 0);
    CHECK(version_cmp("V4", "3.99.1") > 0);

    char b[256];
    CHECK(tuner_base("http://192.168.1.90/lineup.m3u", b, sizeof b) && !strcmp(b, "http://192.168.1.90"));
    CHECK(tuner_base(" http://127.0.0.1:8090/lineup.m3u?x", b, sizeof b) && !strcmp(b, "http://127.0.0.1:8090"));
    CHECK(tuner_base("HTTPS://h", b, sizeof b) && !strcmp(b, "HTTPS://h"));
    CHECK(!tuner_base("/home/x.m3u", b, sizeof b));
    CHECK(!tuner_base("http:///x", b, sizeof b));

    sanitize_name("  a/b:c*  d?\"e\t ", b, sizeof b, 60);
    CHECKS(b, "a b c d e");
    sanitize_name("\xc3\xa9\xc3\xa9\xc3\xa9", b, sizeof b, 5);   /* « ééé » coupé sans casser l'UTF-8 */
    CHECKS(b, "\xc3\xa9\xc3\xa9");

    struct tm tm = { 0 };
    tm.tm_year = 2026 - 1900; tm.tm_mon = 10; tm.tm_mday = 27; tm.tm_hour = 20; tm.tm_min = 5; tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    rec_basename(t, "France 2", "Le journal : 20h", b, sizeof b);
    CHECKS(b, "2026-11-27 20h05 - France 2 - Le journal 20h");
    rec_basename(t, "Arte", "", b, sizeof b);
    CHECKS(b, "2026-11-27 20h05 - Arte");

    fmt_dur(3600, b, sizeof b); CHECKS(b, "1 h");
    fmt_dur(3720, b, sizeof b); CHECKS(b, "1 h 02");
    fmt_dur(300, b, sizeof b); CHECKS(b, "5 min");
    fmt_dur(303, b, sizeof b); CHECKS(b, "5 min 03 s");

    char *e = url_encode("a b/é");
    CHECKS(e, "a%20b%2F%C3%A9");
    free(e);

    CHECK(m3u_attr("#EXTINF:-1 xchannel-number=\"9\" channel-number=\"2\"", "channel-number", b, sizeof b) && !strcmp(b, "2"));
}

static void test_real_guide(const char *path)
{
    char *txt = slurp(path);
    CHECK(txt != NULL);
    if (!txt) return;
    JVal *arr = json_parse(txt);
    CHECK(arr && arr->t == J_ARR);
    Guide g = { 0 };
    long long m = guide_merge(&g, arr);
    int progs = 0;
    for (int i = 0; i < g.n; i++) progs += g.ch[i].n;
    printf("guide réel : %d chaînes, %d programmes, fin %lld\n", g.n, progs, m);
    CHECK(g.n > 0 && progs > 0 && m > 0);
    if (g.n) printf("  1re chaîne %s (%s) : %s\n", g.ch[0].num, g.ch[0].name, g.ch[0].n ? g.ch[0].p[0].title : "-");
    guide_free(&g);
    json_free(arr);
    free(txt);
}

static void test_real_m3u(const char *path)
{
    char *txt = slurp(path);
    CHECK(txt != NULL);
    if (!txt) return;
    ChannelList l;
    int n = playlist_parse(txt, &l);
    printf("playlist réelle : %d chaînes\n", n);
    CHECK(n > 0);
    for (int i = 0; i < l.n && i < 4; i++) printf("  %s | %s | %s\n", l.v[i].num, l.v[i].name, l.v[i].url);
    for (int i = 0; i < l.n; i++) CHECK(l.v[i].num[0] && l.v[i].name[0] && !strncmp(l.v[i].url, "http", 4));
    playlist_free(&l);
    free(txt);
}

int main(int argc, char **argv)
{
    test_json();
    test_playlist();
    test_guide();
    test_tracks();
    test_misc();
    if (argc > 1) test_real_guide(argv[1]);
    if (argc > 2) test_real_m3u(argv[2]);
    printf("%d vérifications, %d échec(s)\n", checks, fails);
    return fails ? 1 : 0;
}
