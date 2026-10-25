# Restart shutdown race, no new GPU workload

The previous Firefox main process had exited, but children were still exiting
when the normal-launch runner checked for existing Firefox processes. The
startup assertion stopped this attempt before launching a new browser or
starting GPU work. After shutdown completed, the next normal-launch attempt
passed. This is a harness startup race, not a GPU fault.
