// ota.c — Remote-Firmware-Update über HTTP-Upload
#include "ota.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "appcfg.h"
#include "auth.h"
#include <string.h>

static const char *TAG = "ota";

// Schutz für die gefährlichen Endpunkte (/ota, /reboot): gültiges Session-Cookie
// (eingeloggter Browser), ?key=<Token> oder Bearer-Token. Offen nur, solange weder
// Passwort noch Token gesetzt sind (Erststart). Zentrale Logik in auth.c.
static bool auth_ok(httpd_req_t *req)
{
    if (auth_http_ok(req)) return true;
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "login or token required (?key= / Bearer)");
    return false;
}

static const char INDEX_HTML[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<title>iHub-Pro OTA</title></head><body style='font-family:sans-serif'>"
    "<h2>Mars Hydro iHub-Pro</h2>"
    "<p>Firmware-Update (.bin hochladen):</p>"
    "<form method='POST' action='/ota' enctype='multipart/form-data'>"
    "<input type='file' name='f'><input type='submit' value='Flashen'></form>"
    "<form method='POST' action='/reboot'><button type='submit'>Neustart</button></form>"
    "</body></html>";

static esp_err_t index_handler(httpd_req_t *req)
{
    if (auth_redirect_if_needed(req)) return ESP_OK;   // nicht eingeloggt → /login
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    if (!auth_ok(req)) return ESP_FAIL;
    httpd_resp_sendstr(req, "Neustart ...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// Nimmt die hochgeladene .bin und schreibt sie in die nächste OTA-Partition.
// Hinweis: einfacher Raw-Body-Empfang (kein Multipart-Parsing) — der Client
// sollte die reine .bin als Body senden (curl --data-binary @firmware.bin).
static esp_err_t ota_handler(httpd_req_t *req)
{
    if (!auth_ok(req)) return ESP_FAIL;
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "keine OTA-Partition");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OTA → Partition '%s' @ 0x%lx (%lu Bytes Upload)",
             part->label, (unsigned long)part->address, (unsigned long)req->content_len);

    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &h);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin fehlgeschlagen");
        return ESP_FAIL;
    }

    char buf[1024];
    int remaining = req->content_len, received = 0;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf));
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_abort(h);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Empfang abgebrochen");
            return ESP_FAIL;
        }
        if (esp_ota_write(h, buf, r) != ESP_OK) {
            esp_ota_abort(h);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_write fehlgeschlagen");
            return ESP_FAIL;
        }
        received += r; remaining -= r;
    }

    if (esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_end/set_boot fehlgeschlagen");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OTA OK (%d Bytes), Reboot ...", received);
    httpd_resp_sendstr(req, "OK — Update geschrieben, Neustart ...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t ota_start(httpd_handle_t *out_server)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 44;       // Reserve für WebUI- + Login- + i18n- + Portal- + Diagnose-URIs
    cfg.recv_wait_timeout = 20;
    cfg.stack_size = 8192;
    cfg.max_open_sockets = 12;       // mehr Parallel-Verbindungen (LWIP_MAX_SOCKETS=16 → max 13)
    cfg.lru_purge_enable = true;     // Sockets voll → älteste Verbindung schließen statt neue
                                     // abzuweisen (sonst „PC drin, Handy kommt nicht mehr rein")

    httpd_handle_t srv = NULL;
    esp_err_t err = httpd_start(&srv, &cfg);
    if (err != ESP_OK) return err;

    // "/" bleibt für das WebUI-Dashboard frei; OTA-Form unter /update.
    httpd_uri_t u_index  = { .uri = "/update", .method = HTTP_GET,  .handler = index_handler };
    httpd_uri_t u_ota    = { .uri = "/ota",    .method = HTTP_POST, .handler = ota_handler };
    httpd_uri_t u_reboot = { .uri = "/reboot", .method = HTTP_POST, .handler = reboot_handler };
    httpd_register_uri_handler(srv, &u_index);
    httpd_register_uri_handler(srv, &u_ota);
    httpd_register_uri_handler(srv, &u_reboot);

    // Hinweis: Das frisch geflashte Image wird NICHT hier als gültig markiert, sondern
    // zeitverzögert nach stabilem Lauf vom Boot-Validator in app_main.c (boot_guard_mark_ok).
    // So kann ein Image, das den Webserver zwar startet aber kurz danach crasht, noch
    // zurückgerollt werden.
    ESP_LOGI(TAG, "HTTP-OTA-Server läuft (Port 80)");
    if (out_server) *out_server = srv;
    return ESP_OK;
}
