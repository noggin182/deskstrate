// HTTPS for a board's web UI, with a Let's Encrypt certificate that CI renews. Shared by
// the core and kb.
//
// The board makes its own key (ECDSA P-256) on first boot and keeps it in NVS: it never
// leaves the board. Its certificate signing request is public and lives in the repo
// (tls/<name>.csr, from GET /api/tls/csr). The tls workflow turns each CSR into a
// certificate for *.<CONFIG_DESKSTRATE_TLS_DOMAIN> (Let's Encrypt, DNS-01) and publishes it
// as an asset of the "tls" release:
//   <ota_pull::kReleases>/download/tls/<name>.pem   leaf + intermediate, PEM
// Like firmware updates, nothing is pushed into the board: it fetches that file itself
// kFirstFetchMs after boot, every kFetchMs, and on the board's update doorbell (ota_pull,
// which the tls workflow rings after publishing), at most once per kMinGapMs. It installs
// it only if it is a certificate for its own key. A new certificate restarts the HTTPS
// server.
//
// HTTPS (port 443) serves the same handlers as the plain server, plus:
//   GET /api/tls       {"name","domain","https","cert":{"subject","issuer","not_after"}|null,
//                       "fetch":{"result","ago_s"}}
//   GET /api/tls/csr   this board's CSR, PEM
// which are on the plain server (port 80) as well. While HTTPS is up, every request to the
// plain server is redirected (307, same method, path and query) to
// https://<name>.<CONFIG_DESKSTRATE_TLS_DOMAIN>/...; without a certificate, or if HTTPS fails
// to start, port 80 serves everything as before. For that the plain server must be started
// with cfg.uri_match_fn = web_tls::uri_match.
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"

namespace web_tls {

inline constexpr uint32_t kFirstFetchMs = 30 * 1000;
inline constexpr uint32_t kFetchMs = 12 * 60 * 60 * 1000;
inline constexpr uint32_t kMinGapMs = 60 * 1000;  // between fetches, doorbell or not
inline constexpr int kUriHandlers = 2;  // handlers start() adds to the HTTPS server
inline constexpr int kHttpUriHandlers = kUriHandlers + 5;  // and to the plain server

// Registers the app's own handlers on a server. Called for the HTTPS server each time it
// (re)starts, so it must be safe to call more than once.
using RegisterFn = esp_err_t (*)(httpd_handle_t server);

// URI matcher for the plain server: exact match (the httpd default) while HTTPS is down,
// and only the redirect handler while it is up.
bool uri_match(const char *tmpl, const char *uri, size_t len);

// name: short board name ("core", "kb"), used for the CSR and certificate file names.
// http: the running plain server, which gets /api/tls and /api/tls/csr.
// Loads or creates the key, starts HTTPS if a certificate is stored, and starts the fetch
// task. Call once Wi-Fi has been started and NVS is initialised.
esp_err_t start(const char *name, httpd_handle_t http, RegisterFn register_fn);

}  // namespace web_tls
