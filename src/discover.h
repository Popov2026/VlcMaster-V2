/*
 * discover.h — détection des HDHomeRun du réseau local (bouton « Détecter » de
 * Paramètres › Source).
 *
 * Protocole de découverte de SiliconDust : un paquet UDP diffusé sur le port
 * 65001 (« qui est là ? », type 0x0002, tous types d'appareils), auquel chaque
 * HDHomeRun répond (type 0x0003) depuis son adresse IP.  Le paquet est envoyé à
 * 255.255.255.255 et à l'adresse de diffusion de chaque carte réseau (PC à
 * plusieurs cartes).  Chaque réponse est confirmée en lisant http://IP/discover.json
 * (modèle, nom, identifiant).  Tout reste sur le réseau local.
 *
 * Inclus par vlcmaster.c après tuner.h.
 */
#include <iphlpapi.h>

#define DISC_MAX 8
typedef struct {
    int n;
    struct { char ip[16]; wchar_t model[40], name[80], id[16]; } d[DISC_MAX];
} DiscResult;
static int g_discBusy;

static unsigned long DiscCrc(const unsigned char *p, int n)
{
    unsigned long c = 0xFFFFFFFFul;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320ul & (0ul - (c & 1))); }
    return c ^ 0xFFFFFFFFul;
}

/* Paquet « découverte » : type, longueur, balises type d'appareil et identifiant = joker, CRC32. */
static int DiscPacket(unsigned char *b)
{
    static const unsigned char body[] = { 0x01, 0x04, 0xFF, 0xFF, 0xFF, 0xFF,     /* type d'appareil : tous */
                                          0x02, 0x04, 0xFF, 0xFF, 0xFF, 0xFF };   /* identifiant : tous */
    b[0] = 0x00; b[1] = 0x02; b[2] = 0x00; b[3] = (unsigned char)sizeof body;
    memcpy(b + 4, body, sizeof body);
    int n = 4 + (int)sizeof body;
    unsigned long c = DiscCrc(b, n);
    b[n++] = (unsigned char)c; b[n++] = (unsigned char)(c >> 8); b[n++] = (unsigned char)(c >> 16); b[n++] = (unsigned char)(c >> 24);
    return n;
}

static void DiscSendAll(SOCKET s, const unsigned char *pk, int n)
{
    struct sockaddr_in to; memset(&to, 0, sizeof to);
    to.sin_family = AF_INET; to.sin_port = htons(65001);
    to.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(s, (const char *)pk, n, 0, (struct sockaddr *)&to, sizeof to);
    /* Diffusion sur chaque carte réseau (255.255.255.255 ne sort que par une seule). */
    ULONG sz = 0;
    if (GetAdaptersInfo(NULL, &sz) != ERROR_BUFFER_OVERFLOW || !sz) return;
    IP_ADAPTER_INFO *ai = (IP_ADAPTER_INFO *)malloc(sz);
    if (ai && GetAdaptersInfo(ai, &sz) == NO_ERROR) {
        for (IP_ADAPTER_INFO *a = ai; a; a = a->Next)
            for (IP_ADDR_STRING *ip = &a->IpAddressList; ip; ip = ip->Next) {
                unsigned long addr = inet_addr(ip->IpAddress.String), mask = inet_addr(ip->IpMask.String);
                if (!addr || addr == INADDR_NONE || !mask) continue;
                to.sin_addr.s_addr = addr | ~mask;
                sendto(s, (const char *)pk, n, 0, (struct sockaddr *)&to, sizeof to);
            }
    }
    free(ai);
}

/* Réponse valide d'un HDHomeRun ? (type 0x0003 et CRC correct) */
static int DiscReplyOk(const unsigned char *b, int n)
{
    if (n < 8 || b[0] != 0x00 || b[1] != 0x03) return 0;
    int len = (b[2] << 8) | b[3];
    if (4 + len + 4 > n) return 0;
    unsigned long c = DiscCrc(b, 4 + len);
    const unsigned char *t = b + 4 + len;
    return t[0] == (c & 0xFF) && t[1] == ((c >> 8) & 0xFF) && t[2] == ((c >> 16) & 0xFF) && t[3] == ((c >> 24) & 0xFF);
}

static DWORD WINAPI DiscThread(LPVOID pv)
{
    (void)pv;
    DiscResult *r = (DiscResult *)calloc(1, sizeof *r);
    WSADATA wd;
    if (r && !WSAStartup(MAKEWORD(2, 2), &wd)) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s != INVALID_SOCKET) {
            BOOL on = TRUE; setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof on);
            struct sockaddr_in me; memset(&me, 0, sizeof me); me.sin_family = AF_INET;
            bind(s, (struct sockaddr *)&me, sizeof me);
            unsigned char pk[32]; int pn = DiscPacket(pk);
            for (int round = 0; round < 2; round++) {           /* deux envois (paquet UDP perdu) */
                DiscSendAll(s, pk, pn);
                ULONGLONG end = GetTickCount64() + 1000;
                for (;;) {
                    ULONGLONG now = GetTickCount64();
                    if (now >= end) break;
                    fd_set fs; FD_ZERO(&fs); FD_SET(s, &fs);
                    struct timeval tv = { 0, (long)((end - now) * 1000) };
                    if (select(0, &fs, NULL, NULL, &tv) <= 0) break;
                    unsigned char b[2048]; struct sockaddr_in from; int fl = sizeof from;
                    int n = recvfrom(s, (char *)b, sizeof b, 0, (struct sockaddr *)&from, &fl);
                    if (n <= 0 || !DiscReplyOk(b, n)) continue;
                    char ip[16]; snprintf(ip, sizeof ip, "%s", inet_ntoa(from.sin_addr));
                    int dup = 0; for (int i = 0; i < r->n; i++) if (!strcmp(r->d[i].ip, ip)) dup = 1;
                    if (!dup && r->n < DISC_MAX) snprintf(r->d[r->n++].ip, 16, "%s", ip);
                }
            }
            closesocket(s);
        }
        WSACleanup();
    }
    /* Confirmation et nom de chaque tuner (discover.json). */
    for (int i = 0; r && i < r->n; i++) {
        wchar_t url[96], err[256]; size_t len = 0;
        swprintf(url, 96, L"http://%hs/discover.json", r->d[i].ip);
        char *buf = ReadHttp(url, &len, err);
        if (buf) {
            JV j; if (JVal(buf, &j, 0) && j.t == J_OBJ) {
                JStrW(&j, "ModelNumber", r->d[i].model, 40);
                JStrW(&j, "FriendlyName", r->d[i].name, 80);
                JStrW(&j, "DeviceID", r->d[i].id, 16);
            }
            JFree(&j); free(buf);
        }
    }
    PostMessageW(g_main, WM_APP_DISC, 0, (LPARAM)r);
    return 0;
}

/* Bouton « Détecter » : cherche les HDHomeRun du réseau (≈ 2 s). */
static void DiscStart(void)
{
    if (g_discBusy) return;
    HANDLE t = CreateThread(NULL, 0, DiscThread, NULL, 0, NULL);
    if (!t) return;
    CloseHandle(t);
    g_discBusy = 1;
    if (g_set) { EnableWindow(U.detect, FALSE); SetWindowTextW(U.scanInfo, TR(L"Recherche des HDHomeRun sur le réseau local…")); }
}

/* Adopte un tuner : sa lineup devient la playlist, rechargée tout de suite. */
static void DiscUse(const DiscResult *r, int i)
{
    swprintf(C.playlist, 2048, L"http://%hs/lineup.m3u", r->d[i].ip);
    if (g_set) SetWindowTextW(U.url, C.playlist);
    g_cur = -1;
    LoadPlaylist();
    GuideLoad();
    SaveConfig();
    wchar_t t[200];
    swprintf(t, 200, TR(L"HDHomeRun trouvé : %ls (%hs)"), r->d[i].name[0] ? r->d[i].name : (r->d[i].model[0] ? r->d[i].model : L"HDHomeRun"), r->d[i].ip);
    Flash(t);
    if (g_set) SetWindowTextW(U.scanInfo, t);
}

/* Résultat de la détection : un tuner = adopté ; plusieurs = menu ; aucun = message. */
static void OnDisc(DiscResult *r)
{
    g_discBusy = 0;
    if (g_set) EnableWindow(U.detect, TRUE);
    HWND owner = g_set ? g_set : g_main;
    if (!r || !r->n) {
        if (g_set) SetWindowTextW(U.scanInfo, L"");
        MessageBoxW(owner, TR(L"Aucun HDHomeRun trouvé sur le réseau local.\n\nVérifiez que le tuner est allumé et branché sur le même réseau que ce PC, et que le pare-feu de Windows autorise HomeRun Player (réseau privé)."),
                    APP_NAME, MB_ICONINFORMATION);
    } else if (r->n == 1) DiscUse(r, 0);
    else {
        HMENU m = CreatePopupMenu();
        for (int i = 0; i < r->n; i++) {
            wchar_t t[200];
            swprintf(t, 200, L"%ls  —  %hs%ls%ls", r->d[i].name[0] ? r->d[i].name : L"HDHomeRun", r->d[i].ip,
                     r->d[i].model[0] ? L"  ·  " : L"", r->d[i].model);
            AppendMenuW(m, MF_STRING, (UINT_PTR)(i + 1), t);
        }
        POINT p; GetCursorPos(&p);
        int c = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, owner, NULL);
        DestroyMenu(m);
        if (c >= 1 && c <= r->n) DiscUse(r, c - 1);
        else if (g_set) SetWindowTextW(U.scanInfo, L"");
    }
    free(r);
}
