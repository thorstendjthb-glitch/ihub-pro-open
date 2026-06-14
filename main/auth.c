// auth.c — Web UI login (sessions) + unified HTTP authorisation. See auth.h.
#include "auth.h"
#include "appcfg.h"
#include "nvs.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "auth";

#define NS            "cfg"          // shared NVS namespace with appcfg
#define SESS_MAX      6              // concurrent logged-in browsers
#define SESS_TTL_S    (7*24*3600)    // cookie lifetime (also re-sent as Max-Age)
#define COOKIE_NAME   "ihub_session"

static char s_user[33]  = "admin";
static char s_phash[65] = "";        // hex SHA-256 of "<salt>:<password>" ("" = no password)
static char s_salt[33]  = "";

typedef struct { char tok[33]; int64_t exp_us; } session_t;
static session_t s_sess[SESS_MAX];

// ── helpers ──────────────────────────────────────────────────────────────────
static int64_t now_us(void) { return esp_timer_get_time(); }

static void rand_hex(char *out, int nbytes)   // out needs nbytes*2 + 1
{
    for (int i = 0; i < nbytes; i++) {
        uint8_t b = (uint8_t)(esp_random() & 0xFF);
        static const char hx[] = "0123456789abcdef";
        out[i*2] = hx[b >> 4]; out[i*2+1] = hx[b & 0xF];
    }
    out[nbytes*2] = 0;
}

static void hash_pw(const char *salt, const char *pass, char out[65])
{
    char in[160];
    int n = snprintf(in, sizeof(in), "%s:%s", salt ? salt : "", pass ? pass : "");
    unsigned char dig[32];
    mbedtls_sha256((const unsigned char *)in, n, dig, 0);   // 0 = SHA-256 (not 224)
    for (int i = 0; i < 32; i++) {
        static const char hx[] = "0123456789abcdef";
        out[i*2] = hx[dig[i] >> 4]; out[i*2+1] = hx[dig[i] & 0xF];
    }
    out[64] = 0;
}

// length-safe, timing-insensitive string compare
static bool ct_eq(const char *a, const char *b)
{
    if (!a || !b) return false;
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (unsigned char)(la ^ lb);
    size_t n = la < lb ? la : lb;
    for (size_t i = 0; i < n; i++) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

static void urldecode(char *s)   // in place: %XX → byte, '+' → space
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') { *o++ = ' '; }
        else if (*p == '%' && p[1] && p[2]) {
            char h[3] = { p[1], p[2], 0 };
            *o++ = (char)strtol(h, NULL, 16); p += 2;
        } else *o++ = *p;
    }
    *o = 0;
}

// extract value of `key` from an x-www-form-urlencoded body into out (decoded)
static bool form_field(const char *body, const char *key, char *out, size_t osz)
{
    size_t kl = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
            p += kl + 1;
            const char *e = strchr(p, '&'); size_t vl = e ? (size_t)(e - p) : strlen(p);
            if (vl >= osz) vl = osz - 1;
            memcpy(out, p, vl); out[vl] = 0;
            urldecode(out);
            return true;
        }
        p = strchr(p, '&'); if (p) p++;
    }
    return false;
}

// ── credentials ──────────────────────────────────────────────────────────────
esp_err_t auth_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_user);  if (nvs_get_str(h, "ui_user", s_user, &sz) != ESP_OK) strcpy(s_user, "admin");
        sz = sizeof(s_phash); if (nvs_get_str(h, "ui_phash", s_phash, &sz) != ESP_OK) s_phash[0] = 0;
        sz = sizeof(s_salt);  if (nvs_get_str(h, "ui_psalt", s_salt,  &sz) != ESP_OK) s_salt[0]  = 0;
        nvs_close(h);
    }
    if (!s_user[0]) strcpy(s_user, "admin");
    ESP_LOGI(TAG, "login: user='%s', password %s", s_user, s_phash[0] ? "set" : "not set (open)");
    return ESP_OK;
}

bool auth_has_password(void) { return s_phash[0] != 0; }

bool auth_required(void)
{
    return s_phash[0] != 0 || appcfg_api_token()[0] != 0;
}

const char *auth_username(void) { return s_user; }

void auth_set_credentials(const char *user, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (user && user[0]) {
        strncpy(s_user, user, sizeof(s_user) - 1); s_user[sizeof(s_user)-1] = 0;
        nvs_set_str(h, "ui_user", s_user);
    }
    if (pass) {
        if (pass[0]) {
            rand_hex(s_salt, 8);                 // fresh 8-byte salt per change
            hash_pw(s_salt, pass, s_phash);
            nvs_set_str(h, "ui_psalt", s_salt);
            nvs_set_str(h, "ui_phash", s_phash);
            ESP_LOGI(TAG, "login password updated");
        } else {                                 // empty → disable login
            s_phash[0] = 0; s_salt[0] = 0;
            nvs_set_str(h, "ui_phash", "");
            nvs_set_str(h, "ui_psalt", "");
            ESP_LOGW(TAG, "login password cleared (UI open)");
        }
    }
    nvs_commit(h); nvs_close(h);
    memset(s_sess, 0, sizeof(s_sess));           // invalidate all sessions on change
}

bool auth_check_login(const char *user, const char *pass)
{
    if (!s_phash[0]) return false;               // no password configured
    if (!ct_eq(user ? user : "", s_user)) return false;
    char hh[65]; hash_pw(s_salt, pass ? pass : "", hh);
    return ct_eq(hh, s_phash);
}

// ── sessions ─────────────────────────────────────────────────────────────────
static void session_new(char out[33])
{
    int slot = 0; int64_t oldest = s_sess[0].exp_us;
    for (int i = 0; i < SESS_MAX; i++) {
        if (s_sess[i].tok[0] == 0 || s_sess[i].exp_us < now_us()) { slot = i; break; }
        if (s_sess[i].exp_us < oldest) { oldest = s_sess[i].exp_us; slot = i; }
    }
    rand_hex(s_sess[slot].tok, 16);
    s_sess[slot].exp_us = now_us() + (int64_t)SESS_TTL_S * 1000000LL;
    strcpy(out, s_sess[slot].tok);
}

static bool session_valid(const char *tok)
{
    if (!tok || !tok[0]) return false;
    for (int i = 0; i < SESS_MAX; i++)
        if (s_sess[i].tok[0] && s_sess[i].exp_us > now_us() && ct_eq(s_sess[i].tok, tok))
            return true;
    return false;
}

static void session_kill(const char *tok)
{
    if (!tok) return;
    for (int i = 0; i < SESS_MAX; i++)
        if (ct_eq(s_sess[i].tok, tok)) { memset(&s_sess[i], 0, sizeof(s_sess[i])); }
}

static bool req_cookie_token(httpd_req_t *req, char *out, size_t osz)
{
    char cookie[256];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK) return false;
    char *p = strstr(cookie, COOKIE_NAME "=");
    if (!p) return false;
    p += strlen(COOKIE_NAME "=");
    const char *e = strchr(p, ';'); size_t vl = e ? (size_t)(e - p) : strlen(p);
    while (vl && (p[0] == ' ')) { p++; vl--; }
    if (vl >= osz) vl = osz - 1;
    memcpy(out, p, vl); out[vl] = 0;
    return true;
}

static bool req_token_ok(httpd_req_t *req)
{
    const char *tok = appcfg_api_token();
    if (!tok[0]) return false;
    char q[256], v[64];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "key", v, sizeof(v)) == ESP_OK && ct_eq(v, tok))
        return true;
    char auth[96];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth, sizeof(auth)) == ESP_OK &&
        strncmp(auth, "Bearer ", 7) == 0 && ct_eq(auth + 7, tok))
        return true;
    return false;
}

bool auth_http_ok(httpd_req_t *req)
{
    if (!auth_required()) return true;            // unconfigured → open
    char ck[33];
    if (req_cookie_token(req, ck, sizeof(ck)) && session_valid(ck)) return true;
    return req_token_ok(req);
}

bool auth_redirect_if_needed(httpd_req_t *req)
{
    if (auth_has_password() && !auth_http_ok(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login");
        httpd_resp_send(req, NULL, 0);
        return true;
    }
    return false;
}

// ── HTTP handlers ─────────────────────────────────────────────────────────────
static const char LOGIN_HTML[] =
    "<!doctype html><html lang=en><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>iHub-Pro Login</title><style>"
    "body{margin:0;background:#0f1419;color:#e6e6e6;font-family:system-ui,sans-serif;"
    "display:flex;min-height:100vh;align-items:center;justify-content:center}"
    ".box{background:#1a212b;padding:28px 26px;border-radius:14px;width:280px;box-shadow:0 8px 30px #0008}"
    "h1{font-size:18px;margin:0 0 4px}p.s{margin:0 0 18px;color:#8aa;font-size:12px}"
    "label{display:block;font-size:12px;color:#9ab;margin:10px 0 4px}"
    "input{width:100%;box-sizing:border-box;padding:9px 10px;border:1px solid #2c3744;"
    "border-radius:8px;background:#0f1419;color:#e6e6e6;font-size:14px}"
    "button{width:100%;margin-top:18px;padding:10px;border:0;border-radius:8px;"
    "background:#2e7d32;color:#fff;font-size:15px;cursor:pointer}"
    "#err{display:none;background:#3a1d1d;color:#ff9a9a;padding:8px 10px;border-radius:8px;"
    "font-size:12px;margin-top:12px}</style></head><body>"
    "<form class=box method=POST action=/api/login>"
    "<h1>\xF0\x9F\x8C\xB1 iHub-Pro</h1><p class=s>Grow controller — please sign in</p>"
    "<label>Username</label><input name=user autocomplete=username value=admin autofocus>"
    "<label>Password</label><input name=pass type=password autocomplete=current-password>"
    "<div id=err>Wrong username or password.</div>"
    "<button type=submit>Sign in</button></form>"
    "<script>if(location.search.indexOf('e=1')>=0)err.style.display='block'</script>"
    "</body></html>";

static esp_err_t login_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, LOGIN_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t login_post(httpd_req_t *req)
{
    char body[512];
    int total = 0, r;
    while (total < (int)sizeof(body) - 1 &&
           (r = httpd_req_recv(req, body + total, sizeof(body) - 1 - total)) > 0)
        total += r;
    body[total > 0 ? total : 0] = 0;

    char user[33] = "", pass[64] = "";
    form_field(body, "user", user, sizeof(user));
    form_field(body, "pass", pass, sizeof(pass));

    if (auth_check_login(user, pass)) {
        char tok[33]; session_new(tok);
        char ck[96];
        snprintf(ck, sizeof(ck), COOKIE_NAME "=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Lax",
                 tok, SESS_TTL_S);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Set-Cookie", ck);
        httpd_resp_set_hdr(req, "Location", "/");
        ESP_LOGI(TAG, "login OK (user '%s')", user);
        return httpd_resp_send(req, NULL, 0);
    }
    ESP_LOGW(TAG, "login FAILED (user '%s')", user);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login?e=1");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t logout_post(httpd_req_t *req)
{
    char ck[33];
    if (req_cookie_token(req, ck, sizeof(ck))) session_kill(ck);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Set-Cookie", COOKIE_NAME "=; Path=/; Max-Age=0; HttpOnly");
    httpd_resp_set_hdr(req, "Location", "/login");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t auth_web_register(httpd_handle_t srv)
{
    httpd_uri_t u_login_g = { .uri = "/login",      .method = HTTP_GET,  .handler = login_get  };
    httpd_uri_t u_login_p = { .uri = "/api/login",  .method = HTTP_POST, .handler = login_post };
    httpd_uri_t u_logout  = { .uri = "/api/logout", .method = HTTP_POST, .handler = logout_post };
    httpd_register_uri_handler(srv, &u_login_g);
    httpd_register_uri_handler(srv, &u_login_p);
    httpd_register_uri_handler(srv, &u_logout);
    return ESP_OK;
}
