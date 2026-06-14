// wifi_portal.c — Captive-Portal für WLAN-Konfiguration
#include "wifi_portal.h"
#include "wifi_conn.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wportal";

static const char PORTAL_HTML[] = R"HTML(<!doctype html><html lang=de><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>iHub-Pro · WLAN</title>
<style>body{background:#0d1117;color:#e6edf3;font:15px/1.5 system-ui,sans-serif;padding:20px;max-width:480px;margin:auto}
h1{font-size:20px}input{width:100%;padding:10px;margin:6px 0 14px;background:#161b22;border:1px solid #30363d;color:#e6edf3;border-radius:8px}
button{background:#2ea043;color:#fff;border:0;border-radius:8px;padding:12px 18px;font-size:15px;font-weight:600;width:100%}
.s{color:#8b949e;font-size:13px}a{color:#3fb950}</style></head><body>
<h1>🌱 iHub-Pro WLAN-Setup</h1>
<p class=s>Wähle dein WLAN und gib das Passwort ein.</p>
<form method=POST action=/wifisave>
<label>Netzwerk (SSID)</label>
<input name=ssid id=ssid list=nets placeholder="WLAN-Name">
<datalist id=nets></datalist>
<label>Passwort</label>
<input name=pass type=password placeholder="WLAN-Passwort">
<button>Speichern & verbinden</button>
</form>
<p><button type=button onclick=scan() style="background:#30363d">🔄 Netzwerke suchen</button></p>
<p class=s id=st></p>
<script>
async function scan(){st.textContent='Suche ...';try{let r=await(await fetch('/wifiscan')).json();
nets.innerHTML=r.map(a=>`<option value="${a.ssid}">${a.ssid} (${a.rssi} dBm)</option>`).join('');
st.textContent=r.length+' Netze gefunden';}catch(e){st.textContent='Scan fehlgeschlagen';}}
scan();
</script></body></html>)HTML";

// %XX und + dekodieren (in-place)
static void urldecode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') { *o++ = ' '; }
        else if (*p == '%' && p[1] && p[2]) {
            char h[3] = { p[1], p[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            p += 2;
        } else *o++ = *p;
    }
    *o = 0;
}

static esp_err_t wifi_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifiscan_get(httpd_req_t *req)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_wifi_scan_start(&sc, true);          // blockierend
    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num > 20) num = 20;
    wifi_ap_record_t recs[20];
    uint16_t got = num;
    esp_wifi_scan_get_ap_records(&got, recs);

    char buf[1100];
    int p = snprintf(buf, sizeof(buf), "[");
    for (int i = 0; i < got && p < (int)sizeof(buf) - 80; i++) {
        // Anführungszeichen in SSID meiden (würde JSON brechen) → überspringen
        if (strchr((char *)recs[i].ssid, '"')) continue;
        p += snprintf(buf + p, sizeof(buf) - p, "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                      (p > 1 ? "," : ""), (char *)recs[i].ssid, recs[i].rssi);
    }
    p += snprintf(buf + p, sizeof(buf) - p, "]");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, p);
}

static esp_err_t wifisave_post(httpd_req_t *req)
{
    char body[300];
    int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) return ESP_FAIL;
    body[n] = 0;

    char ssid[33] = {0}, pass[65] = {0};
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body, "pass", pass, sizeof(pass));
    urldecode(ssid);
    urldecode(pass);

    if (ssid[0] == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID fehlt");
        return ESP_FAIL;
    }
    wifi_save_creds(ssid, pass);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body style='font-family:sans-serif;background:#0d1117;color:#e6edf3;padding:24px'>"
        "<h2>✅ Gespeichert</h2><p>Neustart … das iHub verbindet sich jetzt mit deinem WLAN.</p>"
        "<p>Den Hotspot kannst du verlassen.</p></body></html>");
    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();
    return ESP_OK;
}

void wifi_portal_register(httpd_handle_t srv)
{
    httpd_uri_t u_wifi  = { .uri = "/wifi",     .method = HTTP_GET,  .handler = wifi_get };
    httpd_uri_t u_scan  = { .uri = "/wifiscan", .method = HTTP_GET,  .handler = wifiscan_get };
    httpd_uri_t u_save  = { .uri = "/wifisave", .method = HTTP_POST, .handler = wifisave_post };
    httpd_register_uri_handler(srv, &u_wifi);
    httpd_register_uri_handler(srv, &u_scan);
    httpd_register_uri_handler(srv, &u_save);
    ESP_LOGI(TAG, "WLAN-Portal registriert (/wifi, /wifiscan, /wifisave)");
}

// Captive: jede unbekannte URL → Redirect auf die Setup-Seite.
static esp_err_t captive_404(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/wifi");
    httpd_resp_send(req, "", 0);
    return ESP_OK;
}

// DNS-Hijack: alle A-Anfragen → 192.168.4.1
static void dns_task(void *arg)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { vTaskDelete(NULL); return; }
    struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(53),
                              .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(s); vTaskDelete(NULL); return; }

    uint8_t buf[256];
    struct sockaddr_in from; socklen_t fl = sizeof(from);
    while (1) {
        int n = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 12 || n + 16 > (int)sizeof(buf)) continue;
        buf[2] = 0x81; buf[3] = 0x80;          // Response, no error
        buf[6] = 0; buf[7] = 1;                // ANCOUNT = 1
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        uint8_t *a = buf + n; int i = 0;
        a[i++] = 0xC0; a[i++] = 0x0C;          // Pointer auf Query-Name
        a[i++] = 0x00; a[i++] = 0x01;          // Typ A
        a[i++] = 0x00; a[i++] = 0x01;          // Klasse IN
        a[i++] = 0; a[i++] = 0; a[i++] = 0; a[i++] = 60;   // TTL
        a[i++] = 0; a[i++] = 4;                // RDLEN
        a[i++] = 192; a[i++] = 168; a[i++] = 4; a[i++] = 1;
        sendto(s, buf, n + i, 0, (struct sockaddr *)&from, fl);
    }
}

void wifi_portal_start_captive(httpd_handle_t srv)
{
    httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, captive_404);
    xTaskCreate(dns_task, "dns_hijack", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "Captive-Portal aktiv (DNS-Hijack + 404-Redirect)");
}
