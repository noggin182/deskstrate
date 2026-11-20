#include "web.h"

#include <cstdio>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mirror.h"
#include "web_tls.h"

namespace {

const char *TAG = "web";

// The screen is drawn on a 135 x 240 canvas, scaled up by a whole number so every
// pixel stays square and sharp. The keys are a 2 x 2 grid under it, in controls::Key
// order. LED bytes are squared brightness (neokeys.cpp), so the page shows their
// square root; the backlight dims the canvas the same way.
const char kPage[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Deskstrate deck</title>
<style>
:root{color-scheme:dark}
body{margin:0;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;
  gap:16px;padding:16px;box-sizing:border-box;background:#0b0d10;color:#8a93a0;font:13px system-ui,sans-serif}
#deck{background:#16191e;border-radius:14px;padding:var(--pad);display:flex;flex-direction:column;
  align-items:center;gap:var(--pad);box-shadow:0 8px 30px #0008}
#screen{display:block;image-rendering:pixelated;background:#000;border-radius:3px;
  transition:filter .4s}
#keys{display:grid;grid-template-columns:1fr 1fr;gap:var(--pad);width:100%}
.key{aspect-ratio:1;border-radius:8px;background:#000;border:2px solid #2a2f37;
  transition:background-color .05s,box-shadow .05s}
#status{min-height:1.2em;text-align:center}
</style></head><body>
<div id="deck"><canvas id="screen" width="135" height="240"></canvas>
<div id="keys"><div class="key"></div><div class="key"></div><div class="key"></div><div class="key"></div></div></div>
<div id="status">Connecting…</div>
<script>
const W=135,H=240,cv=document.getElementById('screen'),g=cv.getContext('2d'),
  keys=[...document.querySelectorAll('.key')],st=document.getElementById('status'),
  deck=document.getElementById('deck');
g.fillStyle='#000';g.fillRect(0,0,W,H);
function fit(){
  const s=Math.max(1,Math.floor(Math.min((innerWidth-64)/W,(innerHeight-64)/(H*1.6))));
  cv.style.width=W*s+'px';cv.style.height=H*s+'px';deck.style.setProperty('--pad',4*s+'px');
}
addEventListener('resize',fit);fit();
const lin=b=>Math.round(255*Math.sqrt(b/255));
function state(m){
  if(m.error){st.textContent=m.error;refused=true;ws.close();return}
  if(m.keys)m.keys.forEach((h,i)=>{
    const r=lin(parseInt(h.slice(0,2),16)),gg=lin(parseInt(h.slice(2,4),16)),b=lin(parseInt(h.slice(4,6),16)),
      c=`rgb(${r},${gg},${b})`;
    keys[i].style.backgroundColor=c;keys[i].style.boxShadow=r+gg+b>40?`0 0 18px ${c}`:'none';
  });
  if(m.bl!==undefined){
    cv.style.transitionDuration=(m.fade_ms||0)+'ms';
    cv.style.filter=`brightness(${m.bl?Math.max(.12,Math.sqrt(m.bl/255)):.04})`;
  }
}
function pixels(buf){
  const d=new DataView(buf),x=d.getUint16(0,true),y=d.getUint16(2,true),w=d.getUint16(4,true),h=d.getUint16(6,true);
  const img=g.createImageData(w,h),p=img.data;
  for(let i=0,o=8,q=0;i<w*h;i++,o+=2,q+=4){
    const v=d.getUint16(o,true),r=v>>11,gg=(v>>5)&63,b=v&31;
    p[q]=(r*527+23)>>6;p[q+1]=(gg*259+33)>>6;p[q+2]=(b*527+23)>>6;p[q+3]=255;
  }
  g.putImageData(img,x,y);
}
let ws,retry=500,refused=false;
function connect(){
  refused=false;
  ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');
  ws.binaryType='arraybuffer';
  ws.onopen=()=>{retry=500;st.textContent='Live'};
  ws.onmessage=e=>typeof e.data==='string'?state(JSON.parse(e.data)):pixels(e.data);
  ws.onclose=()=>{
    if(!refused)st.textContent='Reconnecting…';
    setTimeout(connect,refused?10000:retry);retry=Math.min(retry*2,5000);
  };
}
connect();
</script></body></html>)HTML";

esp_err_t page_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kPage, sizeof kPage - 1);
}

esp_err_t status_get(httpd_req_t *req) {
    wifi_ap_record_t ap = {};
    const int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    char buf[320];
    const int n = snprintf(
        buf, sizeof buf,
        "{\"version\":\"%s\",\"uptime_s\":%lld,\"rssi\":%d,"
        "\"ram\":{\"free\":%u,\"largest\":%u,\"min_free\":%u},\"mirror\":{\"viewers\":%d,\"buffered\":%s}}",
        esp_app_get_description()->version, (long long)(esp_timer_get_time() / 1000000), rssi,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), mirror::viewers(),
        mirror::buffered() ? "true" : "false");
    if (n <= 0 || (size_t)n >= sizeof buf) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

// Registers the page, API and WebSocket on a server: the plain one, and the HTTPS one
// each time web_tls (re)starts it.
esp_err_t register_handlers(httpd_handle_t server) {
    // Not aggregate-initialised: with WebSockets on, httpd_uri_t has more fields.
    const struct {
        const char *uri;
        esp_err_t (*handler)(httpd_req_t *);
    } pages[] = {{"/", page_get}, {"/api/status", status_get}};
    for (const auto &p : pages) {
        httpd_uri_t u = {};
        u.uri = p.uri;
        u.method = HTTP_GET;
        u.handler = p.handler;
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &u), TAG, "register %s", u.uri);
    }
    return mirror::register_handler(server);
}

}  // namespace

esp_err_t web_start() {
    ESP_RETURN_ON_ERROR(mirror::start(), TAG, "mirror");

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 16;  // ours (3) + web_tls::kHttpUriHandlers (7)
    cfg.uri_match_fn = web_tls::uri_match;  // redirects everything to HTTPS once it's up
    httpd_handle_t server = nullptr;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &cfg), TAG, "httpd start");
    ESP_RETURN_ON_ERROR(register_handlers(server), TAG, "handlers");

    // HTTPS once a certificate is published. Without one (or if it fails) port 80 still works.
    const esp_err_t tls = web_tls::start("deck", server, register_handlers, mirror::server_stopping);
    if (tls != ESP_OK) ESP_LOGE(TAG, "HTTPS not available: %s", esp_err_to_name(tls));
    ESP_LOGI(TAG, "web page up; %u bytes of internal RAM free, largest block %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return ESP_OK;
}
