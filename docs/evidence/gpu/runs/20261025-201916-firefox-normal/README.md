# Normal Firefox invocation: first-use profile setup

`run.py` launches Firefox without a test profile, automation flags, Mesa path
overrides or rendering preferences, and monitors its own process group for
60 seconds. No GPU faults occur, and native DRI mappings are present.
`state.json` complete means that monitoring finished; it does not prove page
loading. `normal.png`, captured just after monitoring, shows the profile
manager/create-profile wizard with no registered profile. This attempt is
therefore a first-use setup check, not a completed normal browser demo.
The native automation-profile demo is separately validated in
[the native gate](../20261025-201713-firefox-demo-native/).
