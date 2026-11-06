#!/bin/sh
# Creates a self-signed code-signing certificate, "Deskstrate Host Signing", in the login
# keychain, once. `make` signs deskstrate-host with it, so macOS sees every rebuild as the
# same app and keeps its Accessibility permission (an unsigned build is a new app each time).
# macOS asks for your password once, to trust the certificate for code signing.
set -eu
NAME="Deskstrate Host Signing"
KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"

if security find-identity -v -p codesigning | grep -q "\"$NAME\""; then
    echo "\"$NAME\" is already set up."
    exit 0
fi

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cat > "$dir/cert.cnf" <<CNF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $NAME
[ext]
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
CNF
# macOS's own openssl (LibreSSL) writes a .p12 that `security import` accepts.
/usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -config "$dir/cert.cnf" \
    -keyout "$dir/key.pem" -out "$dir/cert.pem" 2>/dev/null
/usr/bin/openssl pkcs12 -export -inkey "$dir/key.pem" -in "$dir/cert.pem" \
    -out "$dir/cert.p12" -passout pass:deskstrate
security import "$dir/cert.p12" -k "$KEYCHAIN" -P deskstrate -T /usr/bin/codesign
security add-trusted-cert -r trustRoot -p codeSign -k "$KEYCHAIN" "$dir/cert.pem"
security find-identity -v -p codesigning | grep "\"$NAME\""
echo "Done. Now make install, then remove and re-add deskstrate-host in Accessibility one last time."
