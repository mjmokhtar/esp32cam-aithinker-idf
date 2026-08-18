/*
 * ESP32-CAM (AI-Thinker) — simple MJPEG web streaming server
 * Pakai library esp32-camera (bukan esp_video, karena ESP32 classic
 * tidak punya peripheral LCD_CAM yang dipakai esp_video/ESP32-S3).
 */

#include <esp_log.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_http_server.h>
#include "esp_camera.h"
#include "camera_pins.h"

// ==== GANTI NAMA/PASSWORD HOTSPOT DI SINI KALAU MAU ====
#define AP_SSID       "ESP32CAM"
#define AP_PASSWORD   "12345678"     // WPA2 minimal 8 karakter, atau "" untuk open (tanpa password)
#define AP_MAX_CONN   4              // maksimal berapa device bisa connect bersamaan
// =========================================================

static const char *TAG = "esp32cam";

static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ---------- WiFi Access Point ----------

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "device connect: " MACSTR, MAC2STR(event->mac));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "device disconnect: " MACSTR, MAC2STR(event->mac));
    }
}

static bool wifi_init_ap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                          &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASSWORD,
            .max_connection = AP_MAX_CONN,
            .authmode = strlen(AP_PASSWORD) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started. SSID:%s password:%s", AP_SSID, AP_PASSWORD);
    ESP_LOGI(TAG, "Connect HP/laptop ke WiFi ini, lalu buka http://192.168.4.1/stream");

    return true;
}

// ---------- Kamera ----------

static esp_err_t init_camera(void)
{
    camera_config_t config = {
        .pin_pwdn       = PWDN_GPIO_NUM,
        .pin_reset      = RESET_GPIO_NUM,
        .pin_xclk       = XCLK_GPIO_NUM,
        .pin_sccb_sda   = SIOD_GPIO_NUM,
        .pin_sccb_scl   = SIOC_GPIO_NUM,
        .pin_d7         = Y9_GPIO_NUM,
        .pin_d6         = Y8_GPIO_NUM,
        .pin_d5         = Y7_GPIO_NUM,
        .pin_d4         = Y6_GPIO_NUM,
        .pin_d3         = Y5_GPIO_NUM,
        .pin_d2         = Y4_GPIO_NUM,
        .pin_d1         = Y3_GPIO_NUM,
        .pin_d0         = Y2_GPIO_NUM,
        .pin_vsync      = VSYNC_GPIO_NUM,
        .pin_href       = HREF_GPIO_NUM,
        .pin_pclk       = PCLK_GPIO_NUM,

        .xclk_freq_hz   = 20000000,
        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,

        .pixel_format   = PIXFORMAT_JPEG,   // OV2640 native JPEG — hemat CPU
        .frame_size     = FRAMESIZE_VGA,    // 640x480, aman untuk RAM tanpa PSRAM besar
        .jpeg_quality   = 12,               // 0-63, makin kecil makin bagus (dan makin besar filenya)
        .fb_count       = 1,
        .fb_location    = CAMERA_FB_IN_PSRAM,
        .grab_mode      = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG, "Camera init success");
    return ESP_OK;
}

// ---------- HTTP stream handler ----------

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
        } else {
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
            if (res == ESP_OK) {
                size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
                res = httpd_resp_send_chunk(req, part_buf, hlen);
            }
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
            }
            esp_camera_fb_return(fb);
            fb = NULL;
        }
        if (res != ESP_OK) {
            break;
        }
    }
    return res;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

static httpd_handle_t start_camera_server(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 1024 * 8;

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &stream_uri);
        httpd_register_uri_handler(server, &capture_uri);
        ESP_LOGI(TAG, "HTTP server started — /stream (MJPEG) dan /capture (single JPEG)");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }

    return server;
}

// ---------- app_main ----------

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (init_camera() != ESP_OK) {
        ESP_LOGE(TAG, "Camera init gagal, cek wiring/board — berhenti di sini");
        return;
    }

    wifi_init_ap();

    start_camera_server();

    ESP_LOGI(TAG, "Siap. Connect ke WiFi \"%s\", lalu buka http://192.168.4.1/stream", AP_SSID);
}