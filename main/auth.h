// auth.h — Web UI login (username + password) with session cookies.
//
// Two independent ways to authenticate against the device:
//   1. UI login  : username + password → salted SHA-256 in NVS → session cookie.
//                  For humans using the dashboard in a browser.
//   2. API token : ?key=<token> or "Authorization: Bearer <token>".
//                  For Home Assistant / scripts / headless OTA (no cookies).
//
// The device is OPEN until at least one of (password, token) is configured, so a
// fresh flash never locks you out. See auth_required().
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

// Load credentials from NVS. Call once at boot, after nvs_flash_init().
esp_err_t auth_init(void);

// True once a UI login password has been set.
bool auth_has_password(void);

// True if the device demands authentication at all (a password OR an API token
// is set). False = unconfigured/open (no lockout on first boot).
bool auth_required(void);

// Configured UI username (default "admin"). Never NULL.
const char *auth_username(void);

// Set credentials. user == NULL keeps the current username. An empty password
// ("") clears the password (UI login disabled). Persisted salted+hashed to NVS.
void auth_set_credentials(const char *user, const char *pass);

// Verify a login attempt against the stored salted hash.
bool auth_check_login(const char *user, const char *pass);

// THE authorisation check for any protected HTTP request. Accepts a valid
// session cookie, a valid ?key=<token>, or "Authorization: Bearer <token>".
// Returns true automatically when the device is unconfigured (auth_required()==false).
bool auth_http_ok(httpd_req_t *req);

// For HTML page handlers: when a PASSWORD is set and the request is not
// authorised, emit a 302 redirect to /login and return true (the caller should
// then return ESP_OK). Token-only setups are left to the API 401 flow.
bool auth_redirect_if_needed(httpd_req_t *req);

// Register /login (GET), /api/login (POST), /api/logout (POST).
esp_err_t auth_web_register(httpd_handle_t srv);
