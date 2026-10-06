import QtQuick
import QtTest
import "../Context.js" as Context

TestCase {
  name: "AskpassContext"

  function test_legacyAndMalformed() {
    compare(Context.validate(undefined), null)
    compare(Context.validate([]), null)
    compare(Context.validate({processes: "ssh"}), null)
    compare(Context.validate({processes: [{pid: 0, executable: "ssh"}]}), null)
    compare(Context.validate({processes: [{pid: 42.5, executable: "ssh"}]}), null)
    compare(Context.validate({processes: [], operation: "reset"}), null)
    compare(Context.validate({processes: [], destination: 12}), null)
    compare(Context.validate({processes: [], cwd: "\ud800"}), null)
    compare(Context.requester(null), "Unknown")
    compare(Context.operation(null, "Challenge:"), "Unknown")
  }

  function test_metadataBounds() {
    compare(Context.byteLength("é🗝"), 6)
    compare(Context.metadataString("x\n\u0000y"), "x  y")
    compare(Context.metadataString("é".repeat(512)), "é".repeat(512))
    compare(Context.metadataString("é".repeat(513)), null)
    var processes = []
    for (var i = 0; i < 16; ++i) processes.push({pid: i + 2, executable: "ssh"})
    verify(Context.validate({processes: processes}) !== null)
    processes.push({pid: 99, executable: "ssh"})
    compare(Context.validate({processes: processes}), null)
  }

  function test_observedSummary() {
    var context = Context.validate({processes: [
      {pid: 42, executable: "/usr/bin/ssh"},
      {pid: 43, executable: "/usr/bin/bash"},
      {pid: 44, executable: "/usr/bin/git"},
      {pid: 45, executable: "/opt/T3Code"},
      {pid: 46, executable: "/usr/lib/systemd/systemd"}
    ], operation: "fetch", destination: "git@work-alias", cwd: "/work/<repo>"})
    compare(Context.requester(context), "ssh ← git ← T3Code")
    compare(Context.operation(context, "Prompt:"), "Git fetch")
    verify(Context.details(context).indexOf("/usr/bin/bash  ·  PID 43") >= 0)
    verify(Context.details(context).indexOf("/work/<repo>") >= 0)
  }

  function test_promptKinds_data() {
    return [
      {tag: "ssh-key", prompt: "Enter passphrase for key '/home/me/.ssh/key': ", kind: "key"},
      {tag: "agent-key", prompt: "Enter passphrase for /home/me/.ssh/key: ", kind: "key"},
      {tag: "key", prompt: "Enter passphrase: ", kind: "key"},
      {tag: "account", prompt: "git@server's password: ", kind: "password"},
      {tag: "password", prompt: "Password:", kind: "password"},
      {tag: "challenge", prompt: "Authentication challenge:", kind: "unknown"},
      {tag: "embedded", prompt: "A server said Enter passphrase for key 'key':", kind: "unknown"}
    ]
  }

  function test_promptKinds(data) {
    compare(Context.kind(data.prompt), data.kind)
  }

  function test_agentHasLocalWording() {
    var context = Context.validate({processes: [{pid: 42, executable: "/usr/bin/ssh-add"}]})
    var prompt = "Enter passphrase for /home/me/.ssh/key: "
    compare(Context.heading(prompt, context), "Unlock a key for the SSH agent")
    compare(Context.operation(context, prompt), "Unlock or add a key to the SSH agent")
    verify(Context.explanation(prompt, context).indexOf("on this machine") >= 0)
    compare(Context.operation(context, "Challenge:"), "Unknown")
  }
}
