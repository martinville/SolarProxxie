# Security model

## Boundaries

This is a local IPv4 appliance with passive telemetry inspection. The normal AP
uses WPA2-PSK and the GUI uses administrator sessions. There are no inverter-write,
remote-command, shell, file-browser or unauthenticated normal-mode OTA endpoints.
Setup is intentionally open but requires first boot or a physical BOOT press.
Its API only accepts connections addressed to the AP interface and expires after
15 minutes. A corrupted or unsupported stored configuration does not silently
open setup; it requires physical recovery.

HTTP management and MQTT TCP are **not encrypted**. Session/CSRF controls protect
request authorization but cannot protect credentials against an attacker who can
observe local plaintext transport. Use a trusted LAN and do not forward management
ports through your router. HTTPS/TLS are future transport additions, not claimed
features of this release.

## Passwords, sessions and storage

Admin passwords are 12–128 bytes and processed using PBKDF2-HMAC-SHA256, a random
16-byte salt and a 32-byte result. New hashes currently use 2,000 iterations to
keep sign-in responsive on the ESP32. Legacy 100,000-iteration hashes are accepted
and migrated after a successful login. Only the salt and result are persisted,
and verification uses a constant-time hash comparison. The current embedded cost
is weak against offline guessing, so use a long, unique password and increase the
cost only after measuring it on the target hardware.

Sessions use 32 random bytes, encoded as hex, with four bounded slots. Cookies
are HttpOnly and SameSite=Strict. Session idle expiry is 30 minutes; the browser
cookie also has a 30-minute lifetime. Login always issues a new token. Mutations
require a separate random CSRF token in a custom header. Login backoff is global
and bounded; a local attacker can temporarily delay other login attempts.

Wi-Fi/MQTT credentials are recoverable because the network stacks require them.
The default firmware stores them in NVS, **not encrypted at rest**. A local flash
reader can recover these credentials. Admin hashes are still salted and not
plaintext passwords. Hiding passwords from the API is not equivalent to flash
encryption. Captures and logs may also contain private identifiers.

## Optional production provisioning

For deployments needing physical flash protection, use Espressif's documented
[flash encryption](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/security/flash-encryption.html)
and [NVS encryption](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/storage/nvs_encryption.html)
procedures, plus secure boot/signing where appropriate. The supplied layout reserves
an NVS-key partition, but merely having that partition does not encrypt secrets.

Flash encryption and secure boot can burn eFuses and change recovery/flashing
procedures. They are deliberately not enabled by development defaults or silently
provisioned by this project. Validate a provisioning process on a spare board and
retain the relevant keys and recovery documentation before production deployment.
Manual GUI OTA verifies the ESP image format/checksum, not a publisher's identity.
Cloud OTA additionally uses certificate-verified HTTPS, a fixed GitHub repository and
asset name, and checks the downloaded image's embedded project name and release
version. These checks protect the selected transport/source but are still not signed
firmware validation. Provision Secure Boot/signed images separately where publisher
identity must be enforced by the device.

## Hostile inputs and resource bounds

Packet parsing uses byte operations and validates lengths before access. TCP flow
state, packet queues, capture storage, logs and sessions have fixed limits. Packet
copy congestion never waits in the lwIP hook. HTTP bodies, headers, URI length,
upload size and socket counts are bounded. JSON settings validate string lengths,
IPv4/masks, non-overlapping networks, intervals, topic characters and duplicate
point identifiers/topic suffixes before saving.

The frontend escapes remote SSIDs and editable names before rendering; it does not
evaluate them as HTML. A content security policy forbids inline script, framing,
external resources and base-URL injection. Normal operational import rejects
critical network/admin fields server-side. No API exports password salts or hashes.

Packet authenticity cannot be established from the reverse-engineered Inteless
plausibility checks. Treat telemetry as monitoring data, not a safety signal.

## Reporting and recovery

Keep private captures out of public issues. Provide sanitized fixture data, the
firmware/build version, and reproduction steps. Use physical BOOT recovery to
replace compromised network/admin settings. A factory reset erases all NVS
configuration; it does not reset irreversible chip security eFuses.
