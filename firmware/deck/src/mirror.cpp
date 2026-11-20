#include "mirror.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace mirror {

namespace {

const char *TAG = "mirror";

constexpr int kWidth = 135;
constexpr int kHeight = 240;
constexpr size_t kShadowBytes = kWidth * kHeight * sizeof(uint16_t);
constexpr size_t kHeader = 8;  // x, y, w, h

struct Viewer {
    httpd_handle_t server;
    int fd;
};

// Viewers: added from the servers' tasks, removed by the sender and server_stopping().
// Only ever held for a few instructions.
portMUX_TYPE viewers_mux = portMUX_INITIALIZER_UNLOCKED;
Viewer viewer_list[kMaxViewers];
int viewer_count = 0;

// The shadow screen, its dirty rectangle and the band buffer. Held by flush() and the
// sender for one memcpy at a time; never while sending.
SemaphoreHandle_t shadow_lock;
uint16_t *shadow = nullptr;   // nullptr while nobody watches
uint8_t *band = nullptr;      // kHeader + kBandBytes
std::atomic<bool> shadow_ready{false};
bool dirty = false;
int dirty_x1, dirty_y1, dirty_x2, dirty_y2;  // inclusive

// Held by the sender for a whole round of sends, so server_stopping() can wait for it.
SemaphoreHandle_t send_lock;
SemaphoreHandle_t job_done;

std::atomic<bool> want_redraw{false};
std::atomic<bool> state_changed{true};
portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t key_rgb[4][3] = {};
uint8_t backlight_duty = 0;
uint32_t backlight_fade_ms = 0;

// One send to every viewer of one server, run in that server's task (httpd's sockets,
// and TLS sessions above all, belong to it).
struct Job {
    httpd_handle_t server;
    const Viewer *viewers;
    int count;
    bool *dead;
    httpd_ws_frame_t frame;
};

void send_job(void *arg) {
    Job *job = static_cast<Job *>(arg);
    for (int i = 0; i < job->count; ++i) {
        const Viewer &v = job->viewers[i];
        if (v.server != job->server || job->dead[i]) continue;
        // The socket may have closed (and its number been reused) since it joined.
        if (httpd_ws_get_fd_info(v.server, v.fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            job->dead[i] = true;
            continue;
        }
        if (httpd_ws_send_frame_async(v.server, v.fd, &job->frame) != ESP_OK) {
            job->dead[i] = true;
            httpd_sess_trigger_close(v.server, v.fd);
        }
    }
    xSemaphoreGive(job_done);
}

// Sends one frame to every viewer in the snapshot; marks the ones that failed.
void broadcast(const Viewer *viewers, int count, bool *dead, httpd_ws_type_t type, uint8_t *data, size_t len) {
    for (int i = 0; i < count; ++i) {
        bool first = !dead[i];
        for (int j = 0; j < i && first; ++j) first = viewers[j].server != viewers[i].server;
        if (!first) continue;
        Job job = {};
        job.server = viewers[i].server;
        job.viewers = viewers;
        job.count = count;
        job.dead = dead;
        job.frame.type = type;
        job.frame.payload = data;
        job.frame.len = len;
        job.frame.final = true;
        if (httpd_queue_work(job.server, send_job, &job) == ESP_OK) {
            xSemaphoreTake(job_done, portMAX_DELAY);
        } else {
            for (int j = i; j < count; ++j) {
                if (viewers[j].server == job.server) dead[j] = true;
            }
        }
    }
}

void forget(const Viewer *viewers, int count, const bool *dead) {
    taskENTER_CRITICAL(&viewers_mux);
    for (int i = 0; i < count; ++i) {
        if (!dead[i]) continue;
        for (int j = 0; j < viewer_count; ++j) {
            if (viewer_list[j].server == viewers[i].server && viewer_list[j].fd == viewers[i].fd) {
                viewer_list[j] = viewer_list[--viewer_count];
                break;
            }
        }
    }
    taskEXIT_CRITICAL(&viewers_mux);
}

void free_shadow() {
    xSemaphoreTake(shadow_lock, portMAX_DELAY);
    // A viewer may have joined (and found the shadow already there) since the snapshot.
    taskENTER_CRITICAL(&viewers_mux);
    const bool watched = viewer_count > 0;
    taskEXIT_CRITICAL(&viewers_mux);
    if (watched) {
        xSemaphoreGive(shadow_lock);
        return;
    }
    shadow_ready = false;
    heap_caps_free(shadow);
    heap_caps_free(band);
    shadow = nullptr;
    band = nullptr;
    dirty = false;
    xSemaphoreGive(shadow_lock);
    ESP_LOGI(TAG, "no viewers; freed the shadow screen (%u bytes free)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

// Allocates the shadow screen if it isn't. From a server's task, on a new viewer.
bool alloc_shadow() {
    xSemaphoreTake(shadow_lock, portMAX_DELAY);
    if (!shadow) {
        shadow = static_cast<uint16_t *>(heap_caps_calloc(1, kShadowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        band = static_cast<uint8_t *>(heap_caps_malloc(kHeader + kBandBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!shadow || !band) {
            heap_caps_free(shadow);
            heap_caps_free(band);
            shadow = nullptr;
            band = nullptr;
        }
        shadow_ready = shadow != nullptr;
    }
    const bool ok = shadow != nullptr;
    xSemaphoreGive(shadow_lock);
    return ok;
}

int state_json(char *out, size_t len) {
    uint8_t rgb[4][3];
    uint8_t duty;
    uint32_t fade;
    taskENTER_CRITICAL(&state_mux);
    memcpy(rgb, key_rgb, sizeof rgb);
    duty = backlight_duty;
    fade = backlight_fade_ms;
    taskEXIT_CRITICAL(&state_mux);
    return snprintf(out, len,
                    "{\"keys\":[\"%02x%02x%02x\",\"%02x%02x%02x\",\"%02x%02x%02x\",\"%02x%02x%02x\"],"
                    "\"bl\":%u,\"fade_ms\":%lu}",
                    rgb[0][0], rgb[0][1], rgb[0][2], rgb[1][0], rgb[1][1], rgb[1][2], rgb[2][0], rgb[2][1],
                    rgb[2][2], rgb[3][0], rgb[3][1], rgb[3][2], duty, (unsigned long)fade);
}

void sender_task(void *) {
    TickType_t last_state = 0;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kFrameMs));
        xSemaphoreTake(send_lock, portMAX_DELAY);

        Viewer viewers[kMaxViewers];
        bool dead[kMaxViewers] = {};
        taskENTER_CRITICAL(&viewers_mux);
        const int count = viewer_count;
        memcpy(viewers, viewer_list, sizeof viewers);
        taskEXIT_CRITICAL(&viewers_mux);

        if (count == 0) {
            if (shadow_ready) free_shadow();
            xSemaphoreGive(send_lock);
            continue;
        }

        if (state_changed && xTaskGetTickCount() - last_state >= pdMS_TO_TICKS(kStateMs)) {
            state_changed = false;
            last_state = xTaskGetTickCount();
            char json[160];
            const int n = state_json(json, sizeof json);
            if (n > 0 && (size_t)n < sizeof json) {
                broadcast(viewers, count, dead, HTTPD_WS_TYPE_TEXT, reinterpret_cast<uint8_t *>(json), n);
            }
        }

        // Take the dirty rectangle; whatever LVGL draws from now on marks it again.
        xSemaphoreTake(shadow_lock, portMAX_DELAY);
        const bool have = dirty && shadow;
        const int x1 = dirty_x1, y1 = dirty_y1, x2 = dirty_x2, y2 = dirty_y2;
        dirty = false;
        xSemaphoreGive(shadow_lock);

        if (have) {
            const int w = x2 - x1 + 1;
            const int rows_per_band = kBandBytes / (w * sizeof(uint16_t));
            for (int y = y1; y <= y2; y += rows_per_band) {
                const int h = (y2 - y + 1) < rows_per_band ? (y2 - y + 1) : rows_per_band;
                xSemaphoreTake(shadow_lock, portMAX_DELAY);
                if (!shadow) {
                    xSemaphoreGive(shadow_lock);
                    break;
                }
                const uint16_t header[4] = {(uint16_t)x1, (uint16_t)y, (uint16_t)w, (uint16_t)h};
                memcpy(band, header, kHeader);
                for (int r = 0; r < h; ++r) {
                    memcpy(band + kHeader + r * w * sizeof(uint16_t), shadow + (y + r) * kWidth + x1,
                           w * sizeof(uint16_t));
                }
                xSemaphoreGive(shadow_lock);
                // band is only replaced by free_shadow(), which runs in this task.
                broadcast(viewers, count, dead, HTTPD_WS_TYPE_BINARY, band, kHeader + w * h * sizeof(uint16_t));
            }
        }

        forget(viewers, count, dead);
        xSemaphoreGive(send_lock);
    }
}

esp_err_t send_text(httpd_req_t *req, const char *text) {
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(text));
    frame.len = strlen(text);
    frame.final = true;
    return httpd_ws_send_frame(req, &frame);
}

esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // The handshake is done: a new viewer.
        const int fd = httpd_req_to_sockfd(req);
        // Listed before the shadow is allocated, so the sender can't free it in between.
        taskENTER_CRITICAL(&viewers_mux);
        const bool added = viewer_count < kMaxViewers;
        if (added) viewer_list[viewer_count++] = {req->handle, fd};
        taskEXIT_CRITICAL(&viewers_mux);
        if (!added) {
            ESP_LOGW(TAG, "viewer refused: already %d", kMaxViewers);
            return send_text(req, "{\"error\":\"The deck already has 2 viewers.\"}");
        }
        if (!alloc_shadow()) {
            const Viewer me = {req->handle, fd};
            const bool gone = true;
            forget(&me, 1, &gone);
            ESP_LOGW(TAG, "viewer refused: no RAM for the shadow screen (%u free, largest %u)",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            return send_text(req, "{\"error\":\"The deck has no RAM free for the mirror.\"}");
        }
        want_redraw = true;
        state_changed = true;
        ESP_LOGI(TAG, "viewer joined (fd %d); %u bytes free, largest block %u", fd,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return ESP_OK;
    }
    // The page sends nothing, but read and drop anything that comes.
    httpd_ws_frame_t frame = {};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK || frame.len == 0) return err;
    uint8_t buf[64];
    if (frame.len > sizeof buf) return ESP_FAIL;
    frame.payload = buf;
    return httpd_ws_recv_frame(req, &frame, frame.len);
}

}  // namespace

void flush(const lv_area_t *area, const lv_color_t *pixels) {
    if (!shadow_ready) return;
    const int x1 = area->x1 < 0 ? 0 : area->x1;
    const int y1 = area->y1 < 0 ? 0 : area->y1;
    const int x2 = area->x2 >= kWidth ? kWidth - 1 : area->x2;
    const int y2 = area->y2 >= kHeight ? kHeight - 1 : area->y2;
    if (x1 > x2 || y1 > y2) return;
    const int stride = area->x2 - area->x1 + 1;
    xSemaphoreTake(shadow_lock, portMAX_DELAY);
    if (shadow) {
        for (int y = y1; y <= y2; ++y) {
            const lv_color_t *src = pixels + (y - area->y1) * stride + (x1 - area->x1);
            uint16_t *dst = shadow + y * kWidth + x1;
#if LV_COLOR_16_SWAP
            for (int x = 0; x <= x2 - x1; ++x) dst[x] = __builtin_bswap16(src[x].full);
#else
            memcpy(dst, src, (x2 - x1 + 1) * sizeof(uint16_t));
#endif
        }
        if (!dirty) {
            dirty = true;
            dirty_x1 = x1;
            dirty_y1 = y1;
            dirty_x2 = x2;
            dirty_y2 = y2;
        } else {
            if (x1 < dirty_x1) dirty_x1 = x1;
            if (y1 < dirty_y1) dirty_y1 = y1;
            if (x2 > dirty_x2) dirty_x2 = x2;
            if (y2 > dirty_y2) dirty_y2 = y2;
        }
    }
    xSemaphoreGive(shadow_lock);
}

void poll() {
    if (want_redraw.exchange(false)) lv_obj_invalidate(lv_scr_act());  // and the layers above it
}

void keys(const uint8_t rgb[4][3]) {
    taskENTER_CRITICAL(&state_mux);
    const bool same = memcmp(key_rgb, rgb, sizeof key_rgb) == 0;
    if (!same) memcpy(key_rgb, rgb, sizeof key_rgb);
    taskEXIT_CRITICAL(&state_mux);
    if (!same) state_changed = true;
}

void backlight(uint8_t duty, uint32_t fade_ms) {
    taskENTER_CRITICAL(&state_mux);
    backlight_duty = duty;
    backlight_fade_ms = fade_ms;
    taskEXIT_CRITICAL(&state_mux);
    state_changed = true;
}

esp_err_t start() {
    shadow_lock = xSemaphoreCreateMutex();
    send_lock = xSemaphoreCreateMutex();
    job_done = xSemaphoreCreateBinary();
    if (!shadow_lock || !send_lock || !job_done) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sender_task, "mirror", 4096, nullptr, 2, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t register_handler(httpd_handle_t server) {
    httpd_uri_t uri = {};
    uri.uri = "/ws";
    uri.method = HTTP_GET;
    uri.handler = ws_handler;
    uri.is_websocket = true;
    return httpd_register_uri_handler(server, &uri);
}

void server_stopping(httpd_handle_t server) {
    if (!send_lock) return;
    xSemaphoreTake(send_lock, portMAX_DELAY);
    taskENTER_CRITICAL(&viewers_mux);
    for (int i = 0; i < viewer_count;) {
        if (viewer_list[i].server == server) {
            viewer_list[i] = viewer_list[--viewer_count];
        } else {
            ++i;
        }
    }
    taskEXIT_CRITICAL(&viewers_mux);
    xSemaphoreGive(send_lock);
}

int viewers() {
    taskENTER_CRITICAL(&viewers_mux);
    const int n = viewer_count;
    taskEXIT_CRITICAL(&viewers_mux);
    return n;
}

bool buffered() { return shadow_ready; }

}  // namespace mirror
