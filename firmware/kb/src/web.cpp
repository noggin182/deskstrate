#include "web.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "kb_link.h"
#include "kb_link_proto.h"
#include "mdns.h"
#include "ota_pull.h"
#include "sleep.h"
#include "web_tls.h"

namespace {

const char *TAG = "web";
constexpr size_t kJsonMax = 3072;

const char kPage[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>deskstrate-kb</title>
<style>
body{font:15px system-ui,sans-serif;margin:16px;max-width:640px;background:#111;color:#ddd}
h1{font-size:20px}td{padding:2px 12px 2px 0;vertical-align:top}td:first-child{color:#888}
pre{background:#1b1b1b;padding:8px;white-space:pre-wrap;min-height:3em}
input{width:70%;background:#1b1b1b;color:#ddd;border:1px solid #444;padding:4px}
</style></head><body>
<h1>Hello from the keyboard</h1>
<table id="t"></table>
<h2>From the keyboard</h2><pre id="rx">...</pre>
<form id="f"><input id="line" maxlength="32" placeholder="text for the keyboard (it echoes it back)"> <button>Send</button></form>
<script>
const row=(k,v)=>`<tr><td>${k}</td><td>${v}</td></tr>`;
const OS=['unsure','Linux','Windows','macOS','iOS'],TR={1:'USB',2:'Bluetooth',4:'2.4 GHz'};
async function poll(){
  try{
    const s=await (await fetch('/api/status')).json();
    const k=s.kb,st=k.status;
    t.innerHTML=row('version',s.version)+row('uptime',s.uptime_s+' s')+row('Wi-Fi',s.rssi+' dBm')+
      row('keyboard',(k.usb?'on USB':'not on USB'+(s.sleep_in_s>=0?`, sleeping in ${s.sleep_in_s} s`:''))+(k.hello?' &middot; '+k.hello:''))+
      (st?row('keyboard status',`${TR[st.transport]||st.transport}, host ${OS[st.host_os]||st.host_os}, layer ${st.base_layer}, battery ${st.battery_pct}%, up ${st.uptime_s} s (${k.status_ago_s} s ago)`)+
        row('keyboard sees',`${st.rx_good} good / ${st.rx_bad} bad frames from here, ${st.resends} resends`):'')+
      row('link',`${k.rx_good} good / ${k.rx_bad} bad frames in, ${k.kvm_actions} KVM keys, ${k.acks} acks`)+
      row('update',s.ota.result+(s.ota.release?' ('+s.ota.release+')':''));
    rx.textContent=k.recent.map(l=>`${l.ago_s}s ago  ${l.text}`).join('\n')||'(nothing yet)';
  }catch(e){}
}
f.onsubmit=async e=>{e.preventDefault();await fetch('/api/kb',{method:'POST',body:line.value});line.value='';setTimeout(poll,300)};
poll();setInterval(poll,2000);
</script></body></html>)HTML";

esp_err_t page_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kPage, sizeof kPage - 1);
}

esp_err_t status_get(httpd_req_t *req) {
    std::unique_ptr<char[]> buf(new (std::nothrow) char[kJsonMax]);
    if (!buf) return httpd_resp_send_500(req);
    wifi_ap_record_t ap = {};
    const int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    int o = snprintf(buf.get(), kJsonMax, "{\"version\":\"%s\",\"uptime_s\":%lld,\"rssi\":%d,\"sleep_in_s\":%d,\"ota\":",
                     esp_app_get_description()->version, (long long)(esp_timer_get_time() / 1000000), rssi,
                     kb_sleep::seconds_left());
    o += ota_pull::status_json(buf.get() + o, kJsonMax - o);
    o += snprintf(buf.get() + o, kJsonMax - o, ",\"kb\":");
    o += kb_link::status_json(buf.get() + o, kJsonMax - o);
    o += snprintf(buf.get() + o, kJsonMax - o, "}");
    if (o <= 0 || (size_t)o >= kJsonMax) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf.get(), o);
}

esp_err_t kb_post(httpd_req_t *req) {
    char line[KB_LINK_MAX_PAYLOAD + 1];
    if (req->content_len >= sizeof line) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too long (max 32)");
    }
    int got = 0;
    while (got < (int)req->content_len) {
        const int r = httpd_req_recv(req, line + got, req->content_len - got);
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    line[got] = 0;
    for (char *c = line; *c; c++) {
        if (*c < 0x20 || *c > 0x7E) *c = ' ';  // one line, printable only
    }
    if (!kb_link::send_text(line)) return httpd_resp_send_500(req);
    return httpd_resp_sendstr(req, "sent\n");
}

void start_mdns(const char *hostname) {
    esp_err_t err = mdns_init();
    if (err == ESP_OK) err = mdns_hostname_set(hostname);
    if (err == ESP_OK) err = mdns_instance_name_set("Deskstrate keyboard");
    if (err == ESP_OK) err = mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    if (err != ESP_OK) ESP_LOGW(TAG, "mDNS: %s", esp_err_to_name(err));
}

// Registers the page and API on a server: the plain one, and the HTTPS one each time
// web_tls (re)starts it.
esp_err_t register_handlers(httpd_handle_t server) {
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page_get, .user_ctx = nullptr},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get, .user_ctx = nullptr},
        {.uri = "/api/kb", .method = HTTP_POST, .handler = kb_post, .user_ctx = nullptr},
    };
    for (const httpd_uri_t &u : uris) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &u), TAG, "register %s", u.uri);
    }
    return ESP_OK;
}

}  // namespace

esp_err_t web_start(const char *hostname) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 16;  // ours (3) + web_tls::kHttpUriHandlers (7)
    cfg.uri_match_fn = web_tls::uri_match;  // redirects everything to HTTPS once it's up
    httpd_handle_t server = nullptr;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &cfg), TAG, "httpd start");
    ESP_RETURN_ON_ERROR(register_handlers(server), TAG, "handlers");
    start_mdns(hostname);
    ESP_LOGI(TAG, "listening on http://%s.local/", hostname);

    // HTTPS once a certificate is published. Without one (or if it fails) port 80 still works.
    const esp_err_t tls = web_tls::start("kb", server, register_handlers);
    if (tls != ESP_OK) ESP_LOGE(TAG, "HTTPS not available: %s", esp_err_to_name(tls));
    return ESP_OK;
}
