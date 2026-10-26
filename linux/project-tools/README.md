# Project preparation tools

The first deployed project is the [CPU image classifier](image-classifier/README.md).
It uses a normal-user venv with native Alpine mathematical/inference libraries.
The [preparation report](../project-readiness.md) records storage, recovery,
package versions, performance and sustained-test evidence.

From the development PC:

```sh
python3 linux/project-tools/backup-workspace
```

This saves a dated SHA256-labelled copy of the board's projects, excluding
`.venv`, under `linux/build/project-workspaces/`. An existing SSH connection to
`root@192.168.1.33` is used; the script does not install credentials or expose
a network service. `--board` and `--destination` can select another SSH target
or PC directory. Archives may contain private project data; their mode is 0600.

Before restoring a workspace, unpack it into a temporary directory and inspect
its contents. Recreate `.venv` as described in the project instructions. The
system recovery archive is separate and contains private configuration; it
must not be published or committed.
