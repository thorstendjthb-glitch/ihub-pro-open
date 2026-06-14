// remote_log.h — Live-Logs über WLAN (TCP, telnet-artig)
//
// Hängt sich in esp_log ein und spiegelt alle Log-Ausgaben an verbundene
// TCP-Clients. Debuggen ohne USB:  nc <ip> 23   oder PuTTY (Raw, Port 23).
// USB-Serial-JTAG-Log läuft parallel weiter.
#pragma once
#include "esp_err.h"

esp_err_t remote_log_start(void);   // startet TCP-Server auf Port 23
