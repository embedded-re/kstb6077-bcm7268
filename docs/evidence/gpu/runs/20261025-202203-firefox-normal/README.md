# Normal-launch setup correction, interrupted harness

The normal-profile setup attempt was still blocked because the test setup
had created `/home/kstb/.mozilla` and its `firefox` parent directory as root.
This run was intentionally stopped by terminating only its Firefox process
group so the two directories could be transferred to kstb. No GPU fault was
observed; `state.json` stopped records the deliberate close, not a GPU crash.
After correcting ownership, the normal default profile was created through
Firefox's `--CreateProfile` command and a fresh normal launch followed.
