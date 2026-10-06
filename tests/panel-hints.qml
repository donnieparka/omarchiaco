import QtQuick
import Quickshell
import qs.Ui
Scope {
 id: test
 property int step: 0
 property var savedCodes: ({})
 property var networks: [{ssid:"alpha",connected:false,known:true},{ssid:"beta",connected:false,known:true}]
 property var devices: [{address:"AA:11",name:"Headphones",connected:false},{address:"BB:22",name:"Mouse",connected:false}]
 property string actedOn: ""
 property int buttonClicks: 0
 property bool failed: false
 function check(condition, message) {
  if (!condition) { failed = true; console.error("FAIL",message) }
  else console.log("PASS",message)
 }
 function wifiEntries() { return engine.entries.filter(e => e.operation === "wifi") }
 function bluetoothEntries() { return engine.entries.filter(e => e.operation === "bluetooth") }
 function key(text) { return {key:text.charCodeAt(0),text:text,modifiers:0,isAutoRepeat:false,accepted:false} }
 Item {
  id: owner
  property string moduleName: "omarchy.network"
  property string passwordSsid: ""
  function networkForSsid(ssid) { return test.networks.filter(n => n.ssid === ssid)[0] || null }
  function deviceFor(row) { return test.devices.filter(d => d.address === row.dev.address)[0] || null }
  function connectDevice(device) { test.actedOn = device.address }
  function disconnectDevice(device) { test.actedOn = "disconnect:" + device.address }
  function connectDirectly(ssid) { test.actedOn = ssid }
  function disconnectRow(ssid) { test.actedOn = "disconnect:" + ssid }
  function openPasswordPrompt(ssid) { passwordSsid = ssid }
  QtObject {
   property bool open: true
   property point cardOrigin: Qt.point(0,0)
   property Item focusTarget: keyArea
  }
  Item {
   id: keyArea
   width:400; height:500
   property bool blocked: false
   Keys.onPressed: event => {}
   Column {
    width:400
    Button { id: narrowButton; width:80; text:"Very long static action"; onClicked:test.buttonClicks++ }
    Button { id: secondButton; width:300; text:"A short label" }
    Item { width:400; height:40; Text { text:"Tap action" } TapHandler {} }
    TextField { id: field; width:80; placeholderText:"Search" }
    Repeater {
     model:test.networks
     Item {
      required property var modelData
      property var net:modelData
      property bool requiresCredentials:false
      width:400; height:50
      Text { text:parent.net.ssid; width:350; font.pixelSize:16 }
      MouseArea {
       anchors.fill:parent
       cursorShape:Qt.PointingHandCursor
       acceptedButtons:Qt.LeftButton
      }
     }
    }
    Repeater {
     model:test.devices
     Item {
      required property var modelData
      property var dev:modelData
      width:400; height:50
      Text { text:parent.dev.name; width:350; font.pixelSize:16 }
      MouseArea { anchors.fill:parent; cursorShape:Qt.PointingHandCursor; acceptedButtons:Qt.LeftButton }
     }
    }
    Text { text:"Volume"; font.pixelSize:16 }
    PanelSlider { id: fakeSlider; value:0.5; onReleased: function(next) { fakeSlider.value = next } }
   }
  }
 }
 QtObject { id:bar; property var activePopout:owner }
 PanelHints { id:engine; barHost:bar }
 Timer {
  interval:100; running:true; repeat:true
  onTriggered:{
   engine.scan()
   if (test.step===0) {
    test.check(engine.entries.length===9,"all initial controls")
    test.check(keyArea.Keys.forwardTo.length===1,"keyboard forwarding attached")
    for (var e of test.wifiEntries()) test.savedCodes[e.row.net.ssid]=e.code
    for (var e of test.bluetoothEntries()) test.savedCodes[e.row.dev.address]=e.code
    test.check(keyArea.clip,"panel content is clipped")
    test.check(engine.entries.some(e => e.kind === "tap"), "tap controls receive hints")
    var action = engine.entries.filter(e => e.item === narrowButton)[0]
    test.check(action.fittedRow.width * action.fittedRow.scale <= narrowButton.width, "long button content fits a narrow control")
    test.check(engine.entries.filter(e => e.item === secondButton)[0].fittedRow.scale === 1, "each button has its own size binding")
    test.check(action.badge.x >= 0 && action.badge.x + action.badge.width <= action.label.width, "badge stays inside its label")
    engine.prefix=test.savedCodes.alpha[0]
    test.networks=[test.networks[1],{ssid:"gamma",connected:false,known:true},test.networks[0]]
    test.devices=[test.devices[1],test.devices[0]]
   } else if(test.step===1) {
    for(var e of test.wifiEntries()) if(test.savedCodes[e.row.net.ssid]) test.check(e.code===test.savedCodes[e.row.net.ssid],"stable code "+e.row.net.ssid)
    for (var e of test.bluetoothEntries()) test.check(e.code===test.savedCodes[e.row.dev.address], "Bluetooth code follows MAC after recreation")
    test.check(engine.activate(test.savedCodes["AA:11"]) && test.actedOn==="AA:11", "Bluetooth resolves current device")
    var gamma=test.wifiEntries().filter(e=>e.row.net.ssid==="gamma")[0]
    test.check(gamma.code!==test.savedCodes.alpha,"new entry gets new code")
    test.check(engine.activate(test.savedCodes.alpha)&&test.actedOn==="alpha","activation resolves current identity")
    test.networks=test.networks.filter(n=>n.ssid!=="alpha")
    test.devices=test.devices.filter(d=>d.address!=="AA:11")
   } else if(test.step===2) {
    test.actedOn=""
    test.check(!engine.activate(test.savedCodes.alpha)&&test.actedOn==="","removed entry cannot activate another")
    test.check(!engine.activate(test.savedCodes["AA:11"]) && test.actedOn==="", "removed Bluetooth device cannot activate another")
    var button=engine.entries.filter(e=>e.kind==="button")[0]
    test.check(engine.activate(button.code)&&test.buttonClicks===1,"button keeps own handler")
    var slider=engine.entries.filter(e=>e.kind==="slider")[0]
    engine.activate(slider.code); engine.adjust(1,"")
    test.check(Math.abs(slider.item.value-0.55)<0.001,"slider keyboard adjustment")
    engine.adjust(0,"end")
    test.check(slider.item.value===slider.item.maximum,"slider End reaches maximum")
    engine.adjusting=""
    engine.prefix=""
    var first=test.key(button.code[0]), second=test.key(button.code[1])
    engine.handleKey(first)
    engine.handleKey(first)
    test.check(engine.prefix===button.code[0],"duplicate delivery of a press is ignored")
    delete engine.heldKeys[first.key]
    engine.handleKey(second)
    delete engine.heldKeys[second.key]
    test.check(test.buttonClicks===2 && engine.prefix==="", "two letters activate exactly once")
    owner.passwordSsid="credentials"
    var typing=test.key("a")
    engine.handleKey(typing)
    test.check(!typing.accepted, "Wi-Fi password entry keeps its letters")
    owner.passwordSsid=""
    keyArea.blocked=true
    var event={key:0,text:"a",modifiers:0,isAutoRepeat:false,accepted:false}
    engine.handleKey(event)
    test.check(!event.accepted,"editing input is untouched")
    bar.activePopout=null
   } else {
    test.check(engine.entries.length===0&&keyArea.Keys.forwardTo.length===0&&!keyArea.clip,"close removes hints and forwarding")
    console.log(test.failed?"HINT_TEST_FAILED":"HINT_TEST_PASSED")
    Qt.quit()
   }
   test.step++
  }
 }
}
