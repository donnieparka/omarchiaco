#!/bin/bash

set -euo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/base-test.sh"

run_node_test <<'JS'
const fs = require('fs')
const source = fs.readFileSync(require('path').join(root, 'shell/plugins/panels/monitor/Panel.qml'), 'utf8')
const match = source.match(/function readBrightness\(\)\s*\{([^{}]*)\}/)
assert(match, 'monitor brightness reader is available for behavioral verification')
const readBrightness = new Function('laptop', 'expanded', 'active', 'brightnessRead', 'brightnessWrite', match[1])

function verifyRead({ label, laptop, expanded = true, active = true, reading = false, writing = false, expected }) {
  let starts = 0
  const brightnessRead = {
    get running() { return reading },
    set running(value) {
      assertEqual(value, true, `${label}: only starts a read`)
      starts++
      reading = value
    }
  }
  readBrightness(laptop, expanded, active, brightnessRead, { running: writing })
  assertEqual(starts, expected, label)
}

verifyRead({ label: 'expanding an active external HP monitor never starts a brightness query', laptop: false, expected: 0 })
verifyRead({ label: 'an external monitor remains query-free while a read is busy', laptop: false, reading: true, expected: 0 })
verifyRead({ label: 'an expanded active laptop panel still reads brightness', laptop: true, expected: 1 })
verifyRead({ label: 'a collapsed laptop card does not read brightness', laptop: true, expanded: false, expected: 0 })
verifyRead({ label: 'an inactive laptop panel does not read brightness', laptop: true, active: false, expected: 0 })
verifyRead({ label: 'a pending laptop read is not started twice', laptop: true, reading: true, expected: 0 })
verifyRead({ label: 'a laptop brightness write blocks a concurrent read', laptop: true, writing: true, expected: 0 })
JS
