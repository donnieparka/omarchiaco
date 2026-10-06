import QtQuick
import QtTest
import Quickshell
import Quickshell.Io
import "Askpass" as Askpass
import "Askpass/Context.js" as Context

ShellRoot {
  id: shell
  property string bridgeOutput: ""
  property int bridgeExit: -1
  Askpass.Service { id: service }
  Socket { id: wire; path: service.socketPath }
  Process {
    id: bridge
    command: [Quickshell.env("ASKPASS_TEST_BINARY"), "Enter passphrase for key '/tmp/dummy-key': "]
    stdout: StdioCollector { onStreamFinished: shell.bridgeOutput = text }
    onExited: function(code, status) { shell.bridgeExit = code }
  }
  TestCase {
    name: "AskpassRealDialog"
    when: true
    function fakeSocket() {
      return {connected: true, writes: [], write: function(v) {this.writes.push(v)}, flush: function() {}}
    }
    function request(prompt, context) {
      var socket = fakeSocket()
      var value = {prompt: prompt}
      if (context !== undefined) value.context = context
      service.receive(socket, JSON.stringify(value))
      wait(250)
      return socket
    }
    function gitContext() {
      return {processes: [{pid: 123, executable: "/usr/bin/ssh"}, {pid: 122, executable: "/usr/bin/git"},
                          {pid: 121, executable: "/opt/t3-code/t3-code"}],
              operation: "fetch", destination: "git@github.com", cwd: "/tmp/dummy-repository"}
    }
    function cleanup() {
      if (qtest_results.failed) console.log("QML_FUNCTION_FAIL", qtest_results.functionName)
      if (service.asking) service.finish(null)
    }
    function capture(card, path) {
      var saved = false
      verify(card.grabToImage(function(image) { saved = image.saveToFile(path) }))
      tryVerify(function() { return saved }, 2000)
    }
    function test_01ContextAndDetails() {
      request("Enter passphrase for key '/tmp/dummy-key': ", gitContext())
      var input = findChild(service, "askpassInput")
      var card = findChild(service, "askpassCard")
      verify(input !== null && card !== null)
      compare(Context.requester(service.context), "ssh ← git ← t3-code")
      compare(Context.operation(service.context, service.prompt), "Git fetch")
      input.text = "dummy secret"
      service.toggleDetails()
      wait(200)
      compare(input.text, "dummy secret")
      verify(input.activeFocus)
      verify(findChild(service, "askpassDetails").visible)
      verify(findChild(service, "askpassDetails").text.indexOf("/tmp/dummy-repository") >= 0)
      service.toggleDetails()
      wait(100)
      capture(card, Quickshell.env("ASKPASS_TEST_OUTPUT") + "/context.png")
    }
    function test_02LegacyAndMalformedContext() {
      request("Unrecognized challenge")
      compare(service.context, null)
      compare(Context.requester(service.context), "Unknown")
      compare(Context.operation(service.context, service.prompt), "Unknown")
      service.finish(null)
      request("Enter passphrase: ", {processes: [{pid: -1, executable: "ssh"}]})
      verify(service.asking)
      compare(service.context, null)
    }
    function test_03PasswordAndAgent() {
      request("dummy@example.invalid's password: ", {processes: [{pid: 1, executable: "/usr/bin/ssh"}]})
      compare(Context.heading(service.prompt, service.context), "Enter the account password")
      service.finish(null)
      request("Enter passphrase for /tmp/dummy-key: ", {processes: [{pid: 2, executable: "/usr/bin/ssh-add"}]})
      compare(Context.heading(service.prompt, service.context), "Unlock a key for the SSH agent")
      compare(Context.operation(service.context, service.prompt), "Unlock or add a key to the SSH agent")
    }
    function test_04BusyAndCleanup() {
      var first = request("Enter passphrase: ", gitContext())
      var input = findChild(service, "askpassInput")
      input.text = "dummy secret"
      var second = fakeSocket()
      service.receive(second, JSON.stringify({prompt: "Must not replace first"}))
      compare(JSON.parse(second.writes[0]).error, "busy")
      compare(service.prompt, "Enter passphrase: ")
      service.finish(null)
      compare(JSON.parse(first.writes[0]).ok, false)
      compare(input.text, "")
      compare(service.context, null)
      compare(service.detailsExpanded, false)
      request("Fresh request")
      compare(input.text, "")
      compare(service.context, null)
    }
    function test_05EnterAndEscape() {
      var socket = request("Enter passphrase: ")
      var input = findChild(service, "askpassInput")
      input.text = "dummy-return"
      input.forceActiveFocus()
      wait(100)
      keyClick(Qt.Key_Return)
      tryCompare(service, "asking", false)
      compare(JSON.parse(socket.writes[0]).response, "dummy-return")
      compare(input.text, "")
      socket = request("Enter passphrase: ")
      input.text = "dummy-cancel"
      input.forceActiveFocus()
      wait(100)
      keyClick(Qt.Key_Escape)
      tryCompare(service, "asking", false)
      compare(JSON.parse(socket.writes[0]).ok, false)
      compare(input.text, "")
    }
    function test_06CBridgeThroughRealDialog() {
      bridge.running = true
      tryCompare(service, "asking", true)
      verify(service.context !== null)
      verify(service.context.processes.length > 0)
      verify(service.context.processes[0].executable.endsWith("/quickshell"))
      var input = findChild(service, "askpassInput")
      wait(250)
      input.text = "dummy-雪-😀"
      input.forceActiveFocus()
      keyClick(Qt.Key_Return)
      tryCompare(bridge, "running", false)
      compare(shell.bridgeExit, 0)
      compare(shell.bridgeOutput, "dummy-雪-😀\n")
      compare(input.text, "")
      compare(service.asking, false)
    }
    function test_07NativeDisconnect() {
      wire.connected = true
      tryCompare(wire, "connected", true)
      wire.write(JSON.stringify({prompt: "Enter passphrase: ", context: gitContext()}) + "\n")
      wire.flush()
      tryCompare(service, "asking", true)
      var input = findChild(service, "askpassInput")
      input.text = "dummy-disconnect"
      service.toggleDetails()
      wire.connected = false
      tryCompare(service, "asking", false)
      compare(input.text, "")
      compare(service.context, null)
      compare(service.detailsExpanded, false)
    }
    function test_08ConstrainedLongText() {
      var context = gitContext()
      context.processes = []
      for (var i = 0; i < 16; ++i) context.processes.push({pid: 100 + i, executable: "/tmp/dummy-program-" + i})
      var prompt = "Enter passphrase for key '/tmp/" + "very-long-key-path/".repeat(30) + "dummy-key-雪': "
      request(prompt, context)
      var card = findChild(service, "askpassCard")
      card.width = 300
      card.height = 240
      service.toggleDetails()
      wait(300)
      var scroll = findChild(service, "askpassScroll")
      var text = findChild(service, "askpassPrompt")
      var input = findChild(service, "askpassInput")
      compare(text.text, "Original SSH prompt:\n" + prompt)
      compare(text.textFormat, Text.PlainText)
      compare(text.wrapMode, Text.WrapAnywhere)
      verify(scroll.contentHeight > scroll.height)
      verify(input.visible && input.height > 0)
      scroll.contentY = scroll.contentHeight - scroll.height
      wait(100)
      capture(card, Quickshell.env("ASKPASS_TEST_OUTPUT") + "/constrained.png")
    }
    function cleanupTestCase() {
      console.log(qtest_results.failCount === 0 ? "QML_CHECK_PASS" : "QML_CHECK_FAIL", qtest_results.failCount)
      Qt.callLater(function() { Qt.quit() })
    }
  }
  Timer { interval: 20000; running: true; onTriggered: {console.log("QML_CHECK_TIMEOUT"); Qt.quit()} }
}
