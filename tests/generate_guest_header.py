# SPDX-License-Identifier: GPL-2.0-only
"""Generate the checked-in guest payload from its AArch64 assembly."""
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
lines = ['/* SPDX-License-Identifier: GPL-2.0-only */', 'unsigned char guest_code[] = {']
for offset in range(0, len(data), 12):
    lines.append('  ' + ', '.join(f'0x{x:02x}' for x in data[offset:offset + 12]) + ',')
lines.extend(['};', f'unsigned int guest_code_len = {len(data)};', ''])
Path(sys.argv[2]).write_text('\n'.join(lines), encoding='ascii')
