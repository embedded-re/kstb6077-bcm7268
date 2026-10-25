# Native Firefox GPU gate

Mesa 23.3.6-r34 under `/usr`, kernel #30. Fresh private automation profile;
no software rendering preferences or Mesa path overrides. Hardware WebRender
and Broadcom V3D 3.3 WebGL confirmed by `graphics.json`; maps and driver hash
confirm native Mesa. Animation, pause/resume, colour button, Wikipedia and
three reload/scroll cycles pass with exact WebGL pixels and zero GPU faults.
The user's Sway PID 5251 stays alive. Test Firefox is closed before the normal
browser launch. `run.py` is the exact executed runner.

See [diagnosis, isolation, regression and installation](../../firefox-vertex-spills-20261025.md).
Run `python3 analyze.py` for offline evidence checks.
