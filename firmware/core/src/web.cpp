#include "web.h"

#include <cstdio>
#include <cstdlib>

#include "ddc.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "hubs.h"
#include "mdns.h"
#include "monitors.h"
#include "ota.h"

namespace {

const char *TAG = "web";

const char kPage[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Deskstrate</title>
<style>
:root{--bg:#f4f4f5;--card:#fff;--text:#18181b;--muted:#71717a;--btn:#e4e4e7;--on:#2563eb;--on-text:#fff;--bad:#dc2626}
@media (prefers-color-scheme:dark){:root{--bg:#09090b;--card:#18181b;--text:#f4f4f5;--muted:#a1a1aa;--btn:#27272a;--on:#3b82f6;--bad:#f87171}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.4 system-ui,sans-serif;padding:16px;max-width:560px;margin:0 auto}
h1{font-size:20px;margin:4px 0 16px}
.row{background:var(--card);border-radius:12px;padding:12px;margin-bottom:12px}
.head{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:8px}
.name{font-weight:600}
.note{color:var(--muted);font-size:13px}
.note.bad{color:var(--bad)}
.btns{display:grid;grid-template-columns:repeat(auto-fit,minmax(70px,1fr));gap:8px}
.btns+.btns{margin-top:8px}
button{font:inherit;padding:14px 4px;border:0;border-radius:8px;background:var(--btn);color:var(--text);cursor:pointer}
button.cur{background:var(--on);color:var(--on-text);font-weight:600}
button:disabled{opacity:.4;cursor:default}
button.busy{opacity:.6}
#msg{color:var(--muted);font-size:13px;min-height:1.4em}
</style>
</head>
<body>
<h1>Deskstrate</h1>
<div id="rows"></div>
<div id="msg"></div>
<script>
const INPUTS=[[15,'PC'],[27,'Mac'],[17,'Guest']];
const POWER=[[1,'On'],[4,'Standby']];
const HUBS=['HID hub','Media hub'];
const PORTS=['PC','Mac','Guest 1','Guest 2'];
const rows=document.getElementById('rows'),msg=document.getElementById('msg');
let state=null,busy=false;

function btn(label,cur,dis,onclick){
  const b=document.createElement('button');
  b.textContent=label;
  if(cur)b.className='cur';
  b.disabled=dis;
  if(onclick)b.onclick=()=>onclick(b);
  return b;
}
function group(btns){const g=document.createElement('div');g.className='btns';btns.forEach(b=>g.append(b));return g}
function row(name,note,bad,groups){
  const r=document.createElement('div');r.className='row';
  const h=document.createElement('div');h.className='head';
  const n=document.createElement('span');n.className='name';n.textContent=name;
  const t=document.createElement('span');t.className='note'+(bad?' bad':'');t.textContent=note;
  h.append(n,t);r.append(h,...groups);return r;
}
function render(){
  rows.replaceChildren();
  (state?state.hub:[{},{}]).forEach((h,i)=>{
    const note=!state?'loading…':h.online?(PORTS[h.port]||'unknown port (status '+h.raw+')'):'not connected'+(h.why?' ('+h.why+')':'');
    const off=!h.online||busy;
    rows.append(row(HUBS[i],note,state&&!h.online,[
      group(PORTS.map((p,port)=>btn(p,h.online&&h.port===port,off,b=>send(b,`/api/hub?i=${i}&port=${port}`)))),
    ]));
  });
  (state?state.mon:[{},{}]).forEach((m,i)=>{
    const name=m.side?m.side[0].toUpperCase()+m.side.slice(1)+' monitor':'Monitor '+(i+1);
    const note=!state?'loading…':m.online?(m.power===4?'standby':'on'):
      !m.lines?'not connected (DDC lines low)':m.edid?'DDC/CI off (enable in the OSD)':'not connected';
    const off=!m.online||busy;
    rows.append(row(name,note,state&&!m.online,[
      group(INPUTS.map(([v,l])=>btn(l,m.online&&m.input===v,off,b=>send(b,`/api/mon?i=${i}&input=${v}`)))),
      group(POWER.map(([v,l])=>btn(l,m.online&&m.power===v,off,b=>send(b,`/api/mon?i=${i}&power=${v}`)))),
    ]));
  });
}
async function refresh(){
  if(busy)return;
  try{const r=await fetch('/api/state');state=await r.json();msg.textContent=''}
  catch(e){msg.textContent='core not reachable'}
  render();
}
async function send(b,url){
  busy=true;b.classList.add('busy');msg.textContent='sending…';
  try{
    const r=await fetch(url,{method:'POST'});
    const j=await r.json();
    if(j.mon)state=j;
    msg.textContent=j.error?('failed: '+j.error):'';
  }catch(e){msg.textContent='core not reachable'}
  busy=false;render();
}
render();refresh();setInterval(refresh,3000);
document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh()});
</script>
</body>
</html>
)HTML";

// Writes {"mon":[...],"hub":[...]} (and "error" if set) into buf. Returns the length.
int state_json(char *buf, size_t len, const char *error) {
    int n = snprintf(buf, len, "{\"mon\":[");
    for (int i = 0; i < monitors::kCount && n < (int)len; i++) {
        const monitors::State s = monitors::get(i);
        const char *side = monitors::side(s.serial);
        n += snprintf(buf + n, len - n,
                      "%s{\"online\":%s,\"input\":%u,\"power\":%u,\"side\":%s%s%s,"
                      "\"lines\":%s,\"edid\":%s}",
                      i ? "," : "", s.online ? "true" : "false", s.input, s.power,
                      side ? "\"" : "", side ? side : "null", side ? "\"" : "",
                      s.lines_up ? "true" : "false", s.edid ? "true" : "false");
    }
    if (n < (int)len) n += snprintf(buf + n, len - n, "],\"hub\":[");
    for (int i = 0; i < hubs::kCount && n < (int)len; i++) {
        const hubs::State s = hubs::get(i);
        n += snprintf(buf + n, len - n, "%s{\"online\":%s,\"port\":%u,\"raw\":\"%08x\",\"why\":\"%s\"}",
                      i ? "," : "", s.online ? "true" : "false", s.port, (unsigned)s.raw,
                      s.error ? s.error : "");
    }
    if (n < (int)len) {
        n += error ? snprintf(buf + n, len - n, "],\"error\":\"%s\"}", error)
                   : snprintf(buf + n, len - n, "]}");
    }
    return n < (int)len ? n : (int)len - 1;
}

// Short reason for the UI, from a monitors::/hubs:: error.
const char *error_text(esp_err_t err) {
    switch (err) {
        case ESP_ERR_NOT_FOUND: return "not connected";
        case ESP_ERR_NOT_FINISHED: return "busy, try again";
        case ESP_ERR_INVALID_ARG: return "bad request";
        default: return esp_err_to_name(err);
    }
}

// Sets the HTTP status for a failed set_*: 400 for a bad request, 409 when the device
// isn't there or is busy, 502 when it didn't take the write.
void set_error_status(httpd_req_t *req, esp_err_t err) {
    switch (err) {
        case ESP_ERR_INVALID_ARG: httpd_resp_set_status(req, "400 Bad Request"); break;
        case ESP_ERR_NOT_FOUND:
        case ESP_ERR_NOT_FINISHED: httpd_resp_set_status(req, "409 Conflict"); break;
        default: httpd_resp_set_status(req, "502 Bad Gateway"); break;
    }
}

esp_err_t send_state(httpd_req_t *req, const char *error) {
    char buf[1024];
    const int n = state_json(buf, sizeof buf, error);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

esp_err_t page_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kPage, sizeof kPage - 1);
}

esp_err_t state_get(httpd_req_t *req) { return send_state(req, nullptr); }

esp_err_t mon_post(httpd_req_t *req) {
    char query[64] = {}, val[16] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof query) != ESP_OK ||
        httpd_query_key_value(query, "i", val, sizeof val) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_state(req, "missing i");
    }
    const int mon = atoi(val);

    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (httpd_query_key_value(query, "input", val, sizeof val) == ESP_OK) {
        err = monitors::set_input(mon, (uint8_t)strtoul(val, nullptr, 0));
    } else if (httpd_query_key_value(query, "power", val, sizeof val) == ESP_OK) {
        err = monitors::set_power(mon, (uint8_t)strtoul(val, nullptr, 0));
    }
    if (err != ESP_OK) set_error_status(req, err);
    return send_state(req, err == ESP_OK ? nullptr : error_text(err));
}

esp_err_t hub_post(httpd_req_t *req) {
    char query[64] = {}, val[16] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof query) != ESP_OK ||
        httpd_query_key_value(query, "i", val, sizeof val) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_state(req, "missing i");
    }
    const int hub = atoi(val);
    if (httpd_query_key_value(query, "port", val, sizeof val) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_state(req, "missing port");
    }
    const esp_err_t err = hubs::set_port(hub, (uint8_t)strtoul(val, nullptr, 0));
    if (err != ESP_OK) set_error_status(req, err);
    return send_state(req, err == ESP_OK ? nullptr : error_text(err));
}

void start_mdns(const char *hostname) {
    esp_err_t err = mdns_init();
    if (err == ESP_OK) err = mdns_hostname_set(hostname);
    if (err == ESP_OK) err = mdns_instance_name_set("Deskstrate core");
    if (err == ESP_OK) err = mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    if (err != ESP_OK) ESP_LOGW(TAG, "mDNS: %s", esp_err_to_name(err));
}

}  // namespace

esp_err_t web_start(const char *hostname) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;  // phones keep idle sockets open; don't run out
    httpd_handle_t server = nullptr;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &cfg), TAG, "httpd start");

    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page_get, .user_ctx = nullptr},
        {.uri = "/api/state", .method = HTTP_GET, .handler = state_get, .user_ctx = nullptr},
        {.uri = "/api/mon", .method = HTTP_POST, .handler = mon_post, .user_ctx = nullptr},
        {.uri = "/api/hub", .method = HTTP_POST, .handler = hub_post, .user_ctx = nullptr},
    };
    for (const httpd_uri_t &u : uris) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &u), TAG, "register %s", u.uri);
    }
    ESP_RETURN_ON_ERROR(ota::register_handlers(server), TAG, "OTA handlers");

    start_mdns(hostname);
    ESP_LOGI(TAG, "listening on http://%s.local/", hostname);
    return ESP_OK;
}
