"""Maintainer utility: generate the declarative map from the checked-in field table.
Not run during ordinary builds. The resulting table is checked in.
"""
from pathlib import Path
import csv, io

root = Path(__file__).resolve().parents[1]
reference=root/'docs/reference-fields.csv'
data = reference.read_text(encoding='utf-8')
units = {'Energy': 'kWh', 'Temperature': '°C', 'Frequency': 'Hz', 'Power': 'W', 'Voltage': 'V', 'Current': 'A', 'Charge': 'Ah', 'StateOfCharge': '%', 'Unitless': ''}
classes = {'Energy': 'energy', 'Temperature': 'temperature', 'Frequency': 'frequency', 'Power': 'power', 'Voltage': 'voltage', 'Current': 'current', 'StateOfCharge': 'battery'}
scales = {'Energy': .1, 'Temperature': .1, 'Frequency': .01}
lines = ['/* Generated from docs/reference-fields.csv; source and license in NOTICE.md.',
         ' * Signedness remains model-specific. */',
         '#include "protocol/protocol.h"', 'const ghost_field_t ghost_fields[] = {']
for r in csv.DictReader(io.StringIO(data)):
    if r['sum_of']:
        lines.append('    {"%s", "%s %s", "W", "power", "measurement", {-1,-1}, {0,0}, {0,0}, 0, GHOST_I16, 1, 0, 0, "Derived sum; all source values required", "%s", {0,0}},' % (r['id'],r['group'],r['name'],r['sum_of']))
        continue
    if not r['v292_offset'] or r['v292_offset'] == '-1':
        continue
    t = r['field_type']
    words = 2 if r['v292_offset2'] else 1
    scale = float(r['scale']) if r['scale'] else scales.get(t, 1)
    def pair(a, b, empty=0):
        return '{%s,%s}' % (r[a] or empty, r[b] or empty)
    lines.append('    {"%s", "%s %s", "%s", "%s", "%s", %s, %s, %s, %d, %s, %s, %s, 0xffffffff, "Single-phase reference; see NOTICE.md", NULL, {0,0}},' % (
        r['id'], r['group'], r['name'], units[t], classes.get(t,''), 'total_increasing' if t == 'Energy' else 'measurement',
        pair('reg','reg2', -1), pair('v292_offset','v292_offset2'), pair('v302_offset','v302_offset2'), words,
        'GHOST_I32' if words == 2 else 'GHOST_I16', scale, -100 if t == 'Temperature' else 0))
lines += ['#include "register_extra.inc"', '};', 'const size_t ghost_field_count = sizeof(ghost_fields) / sizeof(ghost_fields[0]);',
          '_Static_assert(sizeof(ghost_fields) / sizeof(ghost_fields[0]) <= GHOST_FIELDS_MAX, "Increase field capacity");']
(root / 'main/modbus/register_map.c').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('Wrote reference table')
