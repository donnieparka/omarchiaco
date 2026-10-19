#!/bin/bash

set -euo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/base-test.sh"

run_node_test <<'JS'
const monitor = requireFromRoot('shell/plugins/panels/monitor/Model.js')
const display = { name: 'DP-1', width: 3840, height: 2160, scale: 1.5, disabled: false }
const request = { key: '3840x2160', width: 3840, height: 2160 }

assertDeepEqual(monitor.nextScaleRequest([display], {}), request, 'new resolution requests native scale choices')
const cached = { '3840x2160': { values: [1, 1.25, 1.5, 1.6, 2, 2.5, 3, 4], error: '' } }
for (let poll = 0; poll < 5; poll++) {
  assertEqual(monitor.nextScaleRequest([display], cached), null, 'status polling reuses cached native choices')
}
assertEqual(monitor.nextScaleRequest([{ ...display, scale: 2 }], cached), null, 'scale changes reuse resolution choices')
assertEqual(monitor.nextScaleRequest([{ ...display, transform: 1 }], cached), null, 'rotation reuses physical resolution choices')
assertEqual(monitor.nextScaleRequest([{ ...display, name: 'DP-9' }], cached), null, 'connector changes reuse resolution choices')
assertEqual(monitor.nextScaleRequest([display, { ...display, name: 'DP-2' }], cached), null, 'two monitors share choices for the same resolution')
assertDeepEqual(monitor.nextScaleRequest([{ ...display, width: 1920, height: 1080 }], cached),
  { key: '1920x1080', width: 1920, height: 1080 }, 'resolution changes request fresh native choices')
assertDeepEqual(monitor.nextScaleRequest([display, { ...display, width: 6016, height: 3384 }], cached),
  { key: '6016x3384', width: 6016, height: 3384 }, 'cached first monitor does not hide another resolution')
assertEqual(monitor.nextScaleRequest([{ ...display, disabled: true }], {}), null, 'disabled monitors do not request choices')
assertEqual(monitor.nextScaleRequest([{ ...display, width: 0 }], {}), null, 'modeless monitors do not request choices')
assertEqual(monitor.nextScaleRequest([], cached), null, 'disconnected monitors do not request choices')
assertEqual(monitor.nextScaleRequest([display], { '3840x2160': { values: [], error: 'failed' } }), null,
  'failed requests stay cached instead of retrying every poll')
const retry = { ...cached }
delete retry['3840x2160']
assertDeepEqual(monitor.nextScaleRequest([display], retry), request, 'explicit retry requests choices again')
assert(monitor.isCurrentScale(1.5, display), 'current exact preset is highlighted')
assert(!monitor.isCurrentScale(1.6, display), 'other presets are not highlighted')
assert(!monitor.isCurrentScale(1.33, { ...display, scale: 4 / 3 }), 'rounded labels do not select a different scale')
assert(cached['3840x2160'].values.every(value => !monitor.isCurrentScale(value, { ...display, scale: 1.875 })),
  'non-preset current scale does not highlight a preset')
JS
