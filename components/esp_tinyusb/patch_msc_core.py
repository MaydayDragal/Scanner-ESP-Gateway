"""Derive the narrowly patched MSC unit from immutable TinyUSB 0.21.0~2 bytes."""
from pathlib import Path
import hashlib
import sys

EXPECTED_SHA256 = "9bba988f5c1e83db1d565e55e934a8e9dd7f5e5085820b6c555ffa1484473ac5"


def transform(source: bytes) -> bytes:
    if hashlib.sha256(source).hexdigest() != EXPECTED_SHA256:
        raise ValueError("Pinned TinyUSB 0.21.0~2 MSC source mismatch")
    text = source.decode("utf-8")
    patches = [
        ('#include "msc_device.h"', '''#include "class/msc/msc_device.h"

// Scanner gateway extension: completion alone does not establish CSW success.
extern void tinyusb_msc_command_status_cb(uint8_t lun, uint8_t const command[16], bool success);
extern void tinyusb_msc_invalidate_session(void);'''),
        ("void mscd_reset(uint8_t rhport) {", "void mscd_reset(uint8_t rhport) {\n  tinyusb_msc_invalidate_session();"),
        ("static void proc_bot_reset(mscd_interface_t* p_msc) {", "static void proc_bot_reset(mscd_interface_t* p_msc) {\n  tinyusb_msc_invalidate_session();"),
        ("  msc_csw_t * p_csw = &p_msc->csw;\n\n  switch (p_msc->stage)", '''  msc_csw_t * p_csw = &p_msc->csw;

  if (event != XFER_RESULT_SUCCESS) {
    tinyusb_msc_invalidate_session();
    // A failed transfer must never be parsed as a fresh command or written
    // as valid data. Require the ordinary BOT reset/clear-halt recovery.
    p_msc->stage = MSC_STAGE_NEED_RESET;
    usbd_edpt_stall(rhport, p_msc->ep_in);
    usbd_edpt_stall(rhport, p_msc->ep_out);
    return true;
  }
  if (p_msc->stage == MSC_STAGE_STATUS_SENT) {
    tinyusb_msc_command_status_cb(p_cbw->lun, p_cbw->command,
        event == XFER_RESULT_SUCCESS && ep_addr == p_msc->ep_in &&
        xferred_bytes == sizeof(msc_csw_t) && p_csw->status == MSC_CSW_STATUS_PASSED);
  }

  switch (p_msc->stage)'''),
    ]
    for before, after in patches:
        if text.count(before) != 1:
            raise ValueError("Pinned MSC patch anchor mismatch")
        text = text.replace(before, after, 1)
    return text.encode("utf-8")


def main():
    source, target = map(Path, sys.argv[1:])
    patched = transform(source.read_bytes())
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_bytes() != patched:
        target.write_bytes(patched)


if __name__ == "__main__":
    main()
