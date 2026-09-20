# Scanner gateway LED transport override

Base: Espressif **led_strip 3.0.3**, copied byte-for-byte from the resolved registry
package. Upstream repository: https://github.com/espressif/idf-extra-components,
commit `7cd447361ca2f0a1c01aa3089e3031f6171b6c7e`, path `led_strip`.
Registry component hash:
`28621486f77229aaf81c71f5e15d6fbf36c2949cf11094e07090593e659e7639`.

All 32 original files are retained. `LICENSE`, copyright notices,
`idf_component.yml`, `.component_hash`, and `CHECKSUMS.json` preserve the original
registry bytes. The last two describe the upstream package, not this patched tree.
`UPSTREAM_SHA256.json` records every original file's SHA-256.

## Narrow patch

Only `src/led_strip_rmt_dev.c` is modified:

- After successful `rmt_enable`, refresh always attempts `rmt_disable`, including
  transmit and completion failures. The pinned IDF driver requires INIT for the
  next enable; upstream's early returns could leave ENABLE/RUN behind forever.
- If disable fails, the component retains a pending-cleanup flag. The next call
  first retries disable before enabling and transmitting another frame. No
  application code accesses the private handle layout.
- Preserve the original transmit/completion error when cleanup also fails. Log
  the cleanup error; a still-failing cleanup is returned on the next call.
- Limit `rmt_tx_wait_all_done` to 1000 ms instead of an infinite wait. This local
  component serves the gateway's single status pixel. The limit bounds the
  completion wait for an attempted visual transition, excluding SDK scheduling
  and cleanup overhead. Main schedules at most three off attempts a second apart.

`led_strip_clear` remains upstream code: it clears the pixel buffer and uses the
same refresh path. Successful off confirmation requires a successful zero frame
transmission, completion, and cleanup. The application retains a created handle
after initial clear failure so later bounded off attempts can recover it.

## Resolution and verification

The project manifest selects `espressif/led_strip` version `==3.0.3` with
`override_path: ../components/led_strip`; root maintains the exact-pin local lock
rebasing helper. All other package files and public interfaces stay upstream.

Run `python tests/run_led_transport_tests.py`. The harness compiles the actual
application LED driver and the pinned component's `led_strip_api.c` and
`led_strip_rmt_dev.c`. Injected SDK calls model the RMT channel state and physical
pixel output; pixel buffering/clear/refresh are real component code. Cases cover
one-shot transmit and completion faults, cleanup failure/retry, persistent
transport failure with exactly three attempts, bounded completion wait, initial
clear failure, and channel allocation failure. No physical hardware behavior is
claimed. `--upstream` selects the original installed package for red reproduction.
