#include "web_server_esp.h"
#include "camera_module.h"
#include "nxt_bluetooth.h"
#include "state_machine.h"
#include "config.h"

#include "esp_http_server.h"
#include "esp_camera.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// =============================================================================
// web_server_esp.cpp - Implementacja HTTP servera
// Wzorowany na stream_handler z EdgeNN_Streeming, rozszerzony o nowe endpointy
// =============================================================================

// MJPEG streaming constants (z EdgeNN_Streeming)
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY =
    "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static httpd_handle_t _server        = NULL;  // port 80: snap/status/cmd
static httpd_handle_t _stream_server = NULL;  // port 81: MJPEG stream (osobny wątek)

// -----------------------------------------------------------------------------
// Handler: GET / -> MJPEG live stream
// Bezpośrednio zaczerpnięty z EdgeNN_Streeming, uproszczony (bez TFLite/border)
// -----------------------------------------------------------------------------
static esp_err_t stream_handler(httpd_req_t* req) {
    camera_fb_t* fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    // Wyłącz timeout HTTP dla długiego streamu
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    DEBUG_PRINTLN("[WEB] MJPEG stream started");

    while (true) {
        fb = camera_snapshot();
        if (!fb) {
            DEBUG_PRINTLN("[WEB] Stream: camera capture failed");
            res = ESP_FAIL;
            break;
        }

        // Wyślij nagłówek MJPEG part
        size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) {
            camera_fb_return(fb);
            break;
        }

        // Wyślij dane JPEG
        res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        camera_fb_return(fb);
        if (res != ESP_OK) break;

        // Wyślij separator MJPEG boundary
        res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res != ESP_OK) break;
    }

    DEBUG_PRINTLN("[WEB] MJPEG stream ended");
    return res;
}

// -----------------------------------------------------------------------------
// Handler: GET /snap -> pojedynczy JPEG snapshot
// Backend wywołuje ten endpoint gdy chce obraz do Claude Vision
// -----------------------------------------------------------------------------
static esp_err_t snap_handler(httpd_req_t* req) {
    camera_fb_t* fb = camera_snapshot();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snap.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    camera_fb_return(fb);

    DEBUG_PRINTF("[WEB] Snapshot sent: %d bytes\n", (int)fb->len);
    return res;
}

// -----------------------------------------------------------------------------
// Handler: GET /status -> JSON ze stanem robota
// Backend i Web UI odpytują ten endpoint
// -----------------------------------------------------------------------------
static esp_err_t status_handler(httpd_req_t* req) {
    JsonDocument doc;

    doc["wifi_rssi"]      = WiFi.RSSI();
    doc["wifi_ssid"]      = WiFi.SSID();
    doc["ip"]             = WiFi.localIP().toString();
    doc["bt_connected"]   = nxt_is_connected();
    doc["state"]          = sm_state_name();
    doc["uptime_ms"]      = millis();
    doc["camera_ready"]   = camera_is_ready();
    doc["free_heap"]      = ESP.getFreeHeap();

    char buf[256];
    size_t len = serializeJson(doc, buf, sizeof(buf));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, len);

    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Handler: POST /cmd -> komenda ruchu z Web UI lub backendu
// Body JSON: {"direction":"forward","speed":75,"duration_ms":1000}
// -----------------------------------------------------------------------------
static esp_err_t cmd_handler(httpd_req_t* req) {
    // Odczytaj body
    char body[128];
    int body_len = req->content_len;
    if (body_len <= 0 || body_len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, body, body_len);
    if (received <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[received] = '\0';

    // Parsuj JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        DEBUG_PRINTF("[WEB] CMD parse error: %s\n", err.c_str());
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON parse error");
        return ESP_FAIL;
    }

    const char* direction   = doc["direction"] | "stop";
    int         speed       = doc["speed"]       | NXT_DEFAULT_SPEED;
    int         duration_ms = doc["duration_ms"] | 0;

    DEBUG_PRINTF("[WEB] CMD: dir=%s spd=%d dur=%dms\n", direction, speed, duration_ms);

    // Przekaż do modułu NXT bluetooth
    nxt_move(direction, speed, duration_ms);

    // Odpowiedź JSON
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Start / Stop serwera
// -----------------------------------------------------------------------------
void web_server_start() {
    // --- Port 81: tylko strumień MJPEG (własny wątek, nie blokuje pozostałych) ---
    if (_stream_server == NULL) {
        httpd_config_t scfg = HTTPD_DEFAULT_CONFIG();
        scfg.server_port      = 81;
        scfg.ctrl_port        = 32769;  // inny port kontrolny niż domyślny (32768)
        scfg.max_uri_handlers = 2;
        scfg.stack_size       = 8192;
        scfg.max_open_sockets = 2;      // strumień + 1 zapasowy

        if (httpd_start(&_stream_server, &scfg) == ESP_OK) {
            httpd_uri_t uri_stream = {
                .uri     = "/",
                .method  = HTTP_GET,
                .handler = stream_handler,
                .user_ctx = NULL
            };
            httpd_register_uri_handler(_stream_server, &uri_stream);
            DEBUG_PRINTF("[WEB] Stream server started on port 81\n");
        } else {
            DEBUG_PRINTLN("[WEB] Failed to start stream server");
            _stream_server = NULL;
        }
    }

    // --- Port 80: snap / status / cmd (nigdy nie jest blokowany przez strumień) ---
    if (_server != NULL) {
        DEBUG_PRINTLN("[WEB] API server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = HTTP_SERVER_PORT;  // 80
    config.max_uri_handlers = 8;
    config.stack_size       = 8192;

    if (httpd_start(&_server, &config) != ESP_OK) {
        DEBUG_PRINTLN("[WEB] Failed to start API server");
        _server = NULL;
        return;
    }

    httpd_uri_t uri_snap = {
        .uri     = "/snap",
        .method  = HTTP_GET,
        .handler = snap_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(_server, &uri_snap);

    httpd_uri_t uri_status = {
        .uri     = "/status",
        .method  = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(_server, &uri_status);

    httpd_uri_t uri_cmd = {
        .uri     = "/cmd",
        .method  = HTTP_POST,
        .handler = cmd_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(_server, &uri_cmd);

    DEBUG_PRINTF("[WEB] API server started on port %d\n", HTTP_SERVER_PORT);
    DEBUG_PRINTF("[WEB]   GET  http://%s:81/      -> MJPEG stream\n",
                 WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("[WEB]   GET  http://%s/snap     -> JPEG snapshot\n",
                 WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("[WEB]   GET  http://%s/status   -> JSON status\n",
                 WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("[WEB]   POST http://%s/cmd      -> ruch NXT\n",
                 WiFi.localIP().toString().c_str());
}

void web_server_stop() {
    if (_server != NULL) {
        httpd_stop(_server);
        _server = NULL;
        DEBUG_PRINTLN("[WEB] API server stopped");
    }
    if (_stream_server != NULL) {
        httpd_stop(_stream_server);
        _stream_server = NULL;
        DEBUG_PRINTLN("[WEB] Stream server stopped");
    }
}

bool web_server_is_running() {
    return _server != NULL;
}
