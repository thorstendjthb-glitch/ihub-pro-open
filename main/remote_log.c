// remote_log.c — Live-Logs über WLAN (TCP Port 23)
#include "remote_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdio.h>

#define LOG_PORT      23
#define MAX_CLIENTS   2

static int s_clients[MAX_CLIENTS];
static vprintf_like_t s_orig_vprintf;

// esp_log-Hook: erst Original (USB-JTAG), dann an TCP-Clients spiegeln.
static int log_vprintf(const char *fmt, va_list ap)
{
    char buf[256];
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap2);
    va_end(ap2);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i] >= 0 && len > 0) {
            int sent = send(s_clients[i], buf, len > (int)sizeof(buf) ? (int)sizeof(buf) : len,
                            MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent < 0) {  // Client weg
                close(s_clients[i]);
                s_clients[i] = -1;
            }
        }
    }
    // Original (lokale Konsole) zuletzt, mit dem unveränderten va_list
    return s_orig_vprintf ? s_orig_vprintf(fmt, ap) : len;
}

static void server_task(void *arg)
{
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) { vTaskDelete(NULL); return; }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(LOG_PORT),
    };
    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listen_sock, 1) != 0) {
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        struct sockaddr_in src;
        socklen_t sl = sizeof(src);
        int c = accept(listen_sock, (struct sockaddr *)&src, &sl);
        if (c < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) if (s_clients[i] < 0) { slot = i; break; }
        if (slot < 0) { close(c); continue; }   // alle Slots belegt
        const char *hello = "\r\n*** iHub-Pro Remote-Log verbunden ***\r\n";
        send(c, hello, strlen(hello), MSG_NOSIGNAL);
        s_clients[slot] = c;
    }
}

esp_err_t remote_log_start(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) s_clients[i] = -1;
    s_orig_vprintf = esp_log_set_vprintf(log_vprintf);
    xTaskCreate(server_task, "remote_log", 4096, NULL, 4, NULL);
    return ESP_OK;
}
