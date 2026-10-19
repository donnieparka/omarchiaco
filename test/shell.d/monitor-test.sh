#!/bin/bash

set -euo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/base-test.sh"

run_node_test <<'JS'
const monitor = requireFromRoot('shell/plugins/panels/monitor/Model.js')

assertEqual(monitor.clampBrightness(0), 1, 'monitor clamps minimum brightness')
assertEqual(monitor.clampBrightness(101), 100, 'monitor clamps maximum brightness')
assertEqual(monitor.clampBrightness(42.4), 42, 'monitor rounds brightness')
assertEqual(monitor.clampBrightness('nope'), 1, 'monitor rejects invalid brightness')

assertEqual(monitor.normalizeScale('1.250'), '1.25', 'monitor normalizes fractional scale')
assertEqual(monitor.normalizeScale('nope'), '', 'monitor rejects invalid scale')
assertEqual(monitor.brightnessName(96), 'Sun blast', 'monitor names very bright displays')
assertEqual(monitor.brightnessName(12), 'Candlelit', 'monitor names dim displays')

assertDeepEqual(
  monitor.parseDisplays(JSON.stringify([
    { name: 'eDP-1', enabled: true, focused: false, width: 1920, height: 1080 },
    { name: 'HDMI-A-1', enabled: false, focused: false, width: 0, height: 0 },
    { name: 'DP-1', enabled: true, focused: true, width: 1280, height: 800 }
  ])),
  {
    displays: [
      { name: 'eDP-1', enabled: true, focused: false, width: 1920, height: 1080 },
      { name: 'HDMI-A-1', enabled: false, focused: false, width: 0, height: 0 },
      { name: 'DP-1', enabled: true, focused: true, width: 1280, height: 800 }
    ],
    enabledDisplayCount: 2
  },
  'monitor parses display state'
)

assertDeepEqual(monitor.parseDisplays('{'), { displays: [], enabledDisplayCount: 0 }, 'monitor handles invalid display JSON')

const displays = [
  { name: 'DP-2', model: 'External', width: 1920, height: 1080, scale: 1,
    relation: { reference: 'eDP-1', direction: 'right', alignment: 'end' } },
  { name: 'eDP-1', width: 3840, height: 2160, scale: 1.875, relation: null },
  { name: 'DP-1', width: 0, height: 0, disabled: true, relation: null }
]
const originalDisplays = JSON.stringify(displays)
const presented = monitor.presentMonitors(displays)
assertDeepEqual(presented.map(display => display.name), ['eDP-1', 'DP-1', 'DP-2'], 'monitor presentation keeps laptop-first ordering')
assert(presented.every(display => !Object.prototype.hasOwnProperty.call(display, 'relation')), 'monitor presentation excludes obsolete saved relationships')
assertDeepEqual(monitor.presentMonitors(displays.map(display => Object.assign({}, display, {
  relation: { reference: 'DP-1', direction: 'up', alignment: 'start' }
}))), presented, 'legacy relationship changes do not change monitor presentation between polls')
assertEqual(JSON.stringify(displays), originalDisplays, 'monitor presentation leaves controller data unchanged')

const references = presented.filter(display => display.name !== 'eDP-1' && monitor.isActive(display))
assertEqual(monitor.referenceName(references, ''), 'DP-2', 'position defaults to the first other active monitor')
assertEqual(monitor.referenceName(references, 'DP-1'), 'DP-2', 'position replaces an unavailable reference with an active monitor')
assertEqual(monitor.referenceName([{ name: 'DP-1' }, { name: 'DP-2' }], 'DP-2'), 'DP-2', 'position keeps the selected reference while available')
assertEqual(monitor.referenceName([], 'DP-2'), '', 'position has no reference when no other active monitor remains')
JS
