# Pinned MSC command processor for offline host tests

Exact source subset of espressif/tinyusb **0.21.0~2**, registry component hash
`16c8d774dab1f484d3e0c0f61105624d2424bbae864efd19818baf7b451b41e7`,
upstream commit `894ea01409e0407a7dbe0ee29b2d58a4691f9046`.

`LICENSE` is the unmodified upstream MIT license. `UPSTREAM_SHA256.json` records
every included upstream file. No source patches are applied. The subset contains
the production MSC command processor and the headers selected by the host compiler.
`tests/msc_test_config.h` supplies the host configuration with the production
512-byte MSC buffer. Hardware/RTOS boundaries are supplied by the lifecycle harness.

The runner verifies these hashes before compiling. If managed_components contains
TinyUSB, it also verifies that the resolved production files match this subset.
This makes clean-checkout host tests offline while keeping their command processing
identical to the pinned firmware core. Firmware continues to resolve its full
TinyUSB component through the ESP-IDF component manager.
