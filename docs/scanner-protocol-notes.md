# ES-60W network protocol observations

These observations were made with an ES-60W in Wi-Fi Direct mode. They are evidence for the next firmware milestone, not a working scan implementation.

- The scanner assigned the Windows client an address on `192.168.223.0/24`; its gateway and service address was `192.168.223.1`.
- TCP ports 80 and 1865 accepted connections. Ports 443, 3289, 5357, and 8080 did not. HTTP GET requests to `/`, `/eSCL/ScannerCapabilities`, and `/eSCL/ScannerStatus` returned 404. Port 3289 was tested over TCP only; Epson discovery commonly uses UDP.
- The first TCP/1865 connection sent `49 53 80 00 10 0C 00 00 00 05 00 00 01 04 00 00 00` without a request. `49 53` is the ASCII `IS` frame marker and `80 00` is a welcome frame type. This is consistent with Epson's plain TCP ESC/I-2 transport, but the command set still needs confirmation on this model.
- An IS `0x2100` lock request with the seven-byte payload used by SANE's `epsonds` backend received an IS `0xA100` response containing `06` (ACK).
- Sending `INFOx0000000` immediately after that lock produced no response before timeout. The SANE backend performs additional initialization before `INFO`; the missing step needs verification on this scanner. Subsequent connections returned a longer welcome frame containing the client's IP address and closed on a lock request. The meaning of that longer frame is not yet confirmed. Further command probes should avoid leaving a session locked.

The Windows computer's Wi-Fi scan did not reliably show the scanner while associated with a 5 GHz network. Disconnecting it temporarily and scanning again revealed the scanner SSID. The probe script restores the original network and removes its temporary scanner profile.

## Source references

- [Epson's documented network scan service on TCP/1865](https://files.support.epson.com/docid/cpd6/cpd60230.pdf)
- [SANE ESC/I-2 network framing and lock request](https://gitlab.com/sane-project/backends/-/blob/master/backend/epsonds-net.c)
- [SANE initialization and command order](https://gitlab.com/sane-project/backends/-/blob/master/backend/epsonds.c)
- [Independent IS framing and ESC/I-2 protocol notes](https://github.com/mtheuma/epson2paperless/blob/main/docs/PROTOCOL-REFERENCE.md)
