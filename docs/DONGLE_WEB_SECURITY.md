# Dongle local web interface check - 2026-09-17

Target: user-provided `http://192.168.1.16:8080`. Read-only requests only; no
configuration, reboot, or firmware-upload request was sent. The address is on
the local network and results describe the device as observed at test time.

## Confirmed

- `GET /` returned HTTP 200 to a fresh client with no credentials or cookies.
- `GET /config?command=devinfo` also returned HTTP 200 without credentials or
  cookies. Its JSON contains the dongle serial, an eight-character `key`,
  hardware/software versions, and optional APN credentials. The actual key is
  deliberately not stored in this repository.
- Responses use plain HTTP. The observed server banner is `Mongoose/6.7`.
  No HTTPS redirect or authentication challenge was observed on these reads.
- The page itself reads `devinfo` on load and inserts its `sn` and `key` into
  visible HTML. Its source contains client-side actions for Wi-Fi configuration,
  network settings, reboot, and firmware upload.
- The page's reboot action uses an empty `POST /config?command=reboot`. The
  SolarProxxie Reboot control mirrors this request only for a configured,
  connected AP client after administrator confirmation. Show info now reads the
  dongle serial and Register Key from an observed cloud-login packet, without
  calling `devinfo`; the packet-derived values are held only in RAM and returned
  to an authenticated browser on demand. Neither value is persisted to
  configuration or MQTT. The page exposes no factory-reset
  action, and SolarProxxie does not offer one.
- The downloaded PCAP contains the inverter serial but neither the dongle
  serial nor Register Key as ASCII. The web endpoint, not the cloud telemetry
  packets, supplied the dongle identity.

## Reference for the next capture

The dongle serial returned by `devinfo` was `E47123114487`; the inverter
telemetry serial was `2302246241`. The eight-character Register Key is **not
stored here**. Its SHA-256 fingerprint is
`86ee6705716d77dba139653d41778350e445c22fe26caef3aa1888bff2d1308d`.
The full value was given to the owner in the conversation and can be compared
with candidate strings in a future capture.

An unauthenticated, read-only `GET /config?command=server` reported the
configured hostname `pv.e-linter.com` on port `6666`. A DNS lookup on
2026-09-17 returned `120.27.163.58`; DNS answers can change. The current
`solarproxxie-live.pcap` instead shows traffic from the dongle's captured
address `192.168.50.2` to `13.42.117.179:51100` and
`101.37.34.21:51100`. The observed flows cannot yet be equated with the
configured hostname or with a particular authentication step. A startup
capture including DNS and the first TCP connection is needed to establish the
relationship.

## Risks and limits

Any client that can reach this local HTTP service can retrieve the Register Key
with the tested GET request. The key also travels in plaintext on the LAN.
Restrict access to this service to trusted devices and do not expose port 8080
through Internet port forwarding. Check whether the vendor offers an updated
firmware or a way to restrict the management interface. Rotate the Register Key
if the vendor provides a supported method and it has been exposed to others.

The page's JavaScript uses `eval` for JSON and `innerHTML` for returned values,
including scanned SSIDs. This creates a potential browser-script injection
path if an attacker can influence a returned value; exploitability was not
tested. The page's POST code shows no visible authentication or CSRF token, but
the server-side checks for configuration, reboot, and upload endpoints were not
tested because doing so could change or interrupt the dongle.
