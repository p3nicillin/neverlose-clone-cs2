#pragma once

// RTCore64.sys - MSI Afterburner vulnerable driver (signed, public CVE)
// Drop it to disk at runtime; load via SCM; exploit for arbitrary kernel r/w.
// The actual binary must be placed at: loader/src/RTCore64.sys
// Then run: python tools/bin2h.py loader/src/RTCore64.sys loader/src/rtcore64_bin.h
//
// OR: place RTCore64.sys next to neverlose_loader.exe at runtime —
// the loader will look for it there if the embedded array is empty.

// Placeholder: zero-length array causes the loader to look for RTCore64.sys on disk.
extern const unsigned char g_RTCore64[1] = { 0 };
extern const unsigned int  g_RTCore64_len = 0;
