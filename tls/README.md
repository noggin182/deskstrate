# HTTPS certificates

The core and kb serve their web UI over HTTPS on port 443 as well as plain HTTP on 80,
with a Let's Encrypt certificate for `*.deskstrate.nogsnet.com`
(`components/web_tls`). Each board makes its own key on first boot and keeps it in NVS.
Only its certificate signing request (CSR), which is public, comes here.

`.github/workflows/tls.yml` turns each `<board>.csr` in this folder into a certificate,
renews it weekly once it has less than 30 days left, and publishes it as `<board>.pem` on
the `tls` release (a prerelease, so it never counts as "latest"). The board fetches it
30 s after boot and every 12 hours, and installs it only if it matches its own key.

## One-time setup

1. **deSEC** (free DNS with an API, because Squarespace has none): sign up at desec.io
   and choose "Configure your own domain" with `acme.nogsnet.com`, a subdomain used only
   for this. Create an API token, and add it as the repo secret `DESEC_TOKEN`. Optionally,
   set the repo variable `ACME_EMAIL`.
2. **Squarespace DNS for nogsnet.com:**
   - NS `acme` → `ns1.desec.io` and NS `acme` → `ns2.desec.org`: this hands only
     `acme.nogsnet.com` over to deSEC.
   - CNAME `_acme-challenge.deskstrate` → `deskstrate.acme.nogsnet.com` (lego follows it
     and writes the challenge TXT record at deSEC). It must point at a name inside the
     zone, not `acme.nogsnet.com` itself: lego's deSEC plugin can't write at a zone's apex.
   - An A record for each board's LAN address, e.g. `core.deskstrate` → `192.168.4.44`
     and `kb.deskstrate` → the kb's address. Give both boards DHCP reservations so the
     addresses don't move. Don't touch the existing `deskstrate` A record: the doorbells
     use it.
3. Test the DNS side: run the tls workflow by hand with **staging** ticked. It uses Let's
   Encrypt's staging server and publishes nothing.

## Adding a board (or after its key changes)

A local build in an existing checkout keeps its old `sdkconfig.s3_mini`, which doesn't pick
up the new HTTPS settings from `sdkconfig.defaults`. If it fails to compile `web_tls.cpp`,
delete `firmware/<board>/sdkconfig.s3_mini` and build again. The Wi-Fi credentials and OTA
token are already stored in the board's NVS, so they don't need re-entering.

Flash firmware with `web_tls`, then save its CSR here and push to `main`:

```
curl -o tls/core.csr http://deskstrate.local/api/tls/csr
curl -o tls/kb.csr http://deskstrate-kb.local/api/tls/csr
```

The push runs the workflow, and the board picks the certificate up within 12 hours.
Reboot it to pick it up straight away. `GET /api/tls` shows what it has:

```
{"name":"core","domain":"deskstrate.nogsnet.com","https":true,
 "cert":{"subject":"CN=*.deskstrate.nogsnet.com","issuer":"C=US, O=Let's Encrypt, CN=E7",
         "not_after":"2027-02-02T12:00:00Z"},
 "fetch":{"result":"installed, valid until 2027-02-02T12:00:00Z","ago_s":40}}
```

Then browse to `https://core.deskstrate.nogsnet.com/`. If that name doesn't resolve on
the LAN while a public lookup works (`nslookup core.deskstrate.nogsnet.com 1.1.1.1`),
the router's DNS rebind protection is dropping public names that point at private
addresses. Allow `deskstrate.nogsnet.com` in the router, or add local DNS entries for the
boards.

An erased NVS gives the board a new key. Its certificate then no longer matches
(`"fetch":{"result":"not for this board's key ..."}`), so commit its new CSR.
