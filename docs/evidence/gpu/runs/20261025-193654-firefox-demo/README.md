# Firefox demonstration fails with GPU hangs (2026-10-25)

Firefox 151.0.3-r0 was installed through APK without replacing the pinned
native Mesa packages. Launching it as user kstb in the existing Wayland
Sway session, with a fresh test profile and a local WebGL demo, triggered
**24 GPU resets over 24.996 seconds**. Every logged V3D error status is
`0x00001000`. The user observed the browser drawing in chunks.

This is a failed browser graphics milestone. The working compositor,
launcher and simple GLES clients do not establish browser acceleration
reliability. No completed WebDriver session, graphics report, FPS results,
public-webpage validation or successful browser screenshot was obtained.

The test used a separate profile at
`/home/kstb/.mozilla/firefox/kstb-browser-test`, Marionette on localhost and
`--remote-allow-system-access` for planned graphics diagnostics. Its user
preferences suppressed first-run UI but did not force hardware acceleration
or change graphics preferences. The local page requested WebGL1 and ran an
animated shader with periodic pixel checks. Firefox startup/WebRender,
WebGL and compositor interaction were not isolated, so the responsible
component/shader is unknown. An unsupported extension probe in stderr is
not sufficient to identify the hang's cause.

## Monitor failure and cleanup

The original runner checked the kernel log in its main loop, then blocked
waiting for Firefox's startup/automation response. Its kernel logger kept
recording, but it did not independently terminate the browser on the first
reset. That allowed the repeated resets. Firefox was explicitly terminated;
the runner then completed cleanup with state `stopped`.

All Firefox and test-watchdog processes are gone. The existing user-owned
Sway process **5251** remains; it was not restarted and other user apps were
not closed. At the final read-only check, the last reset was over two minutes
old. Actual input/display recovery was asked of the user and remains
unconfirmed. Firefox remains installed. No hardware-accelerated browser
retry was performed after the fault.

The active demo HTML was removed from `/usr/share/kstb/browser-demo` and
preserved as `reproducer.html` here and
`/root/gpu-system-prep/firefox-webgl-reproducer.html` on the board. It is an
unsafe reproducer, not a passing demo. `run.py` is the exact failed runner.
Kernel/stderr logs, the Marionette greeting, counts and final process/package
state are preserved.

## Independent abort guard

`port/gpu-test/kstb_fault_guard.py` now watches the kernel-log file on its
own thread and immediately kills only explicitly attached test process
groups when it sees a fault. Processes must have been launched in a new
session; it never attaches to or signals the user's desktop. A blocked
browser RPC cannot delay its decision. The future browser runner also now
handles EOF while receiving a protocol header.

Two synthetic tests verify killing a blocked test and its child, and a fault
that arrives before a test is attached. They use a temporary fake log and
ordinary sleeping processes, never GPU work or kernel-log writes. Both pass
on the host and board; the board's measured abort latency is about 32 ms.
This is a synthetic guard check, not a browser retest. The revised browser
runner is archived as `runner-with-independent-guard.py` and is untested on
the GPU. No passing-browser result is implied.

A subsequent authorized milestone should first establish a CPU-rendered,
WebGL-disabled browser baseline, then isolate WebRender and WebGL with the
independent abort guard active. No Mesa, kernel, startup or display settings
were changed during this failed attempt.
