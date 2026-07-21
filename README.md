# git-watch

`git-watch` watches a Git worktree, stages filesystem changes as they happen, and periodically synchronizes with `origin`.

It is intended for small personal repositories where automatic syncing is acceptable, such as notes or configuration repos.

## What it does

- Watches a repository path recursively.
- Ignores paths that Git marks as ignored.
- Stages created, modified, and deleted files after a short debounce window.
- Every 60 seconds:
  - fetches `origin`
  - fast-forwards when possible
  - creates an `Autocommit` commit from the current index
  - pushes `refs/heads/master` to `origin`

## Caveats

This tool is small and opinionated.

- It currently pushes only `refs/heads/master`.
- It expects an `origin` remote.
- It creates commits with the fixed message `Autocommit`.
- It uses your repository Git identity for commits.
- It authenticates with SSH credentials from your agent, falling back to `~/.ssh/id_ed25519`.
- It does not resolve non-fast-forward conflicts.

Use it only on repositories where automatic staging, committing, and pushing are desired.

## Usage

```sh
git-watch --path ~/Notes --log-level info --interval 60
```

CLI options:

```text
Usage: git-watch [OPTIONS] --path <PATH>

Options:
  -p, --path <PATH>
      --log-level <LOG_LEVEL>  [default: INFO]
      --interval <INTERVAL>    [default: 60]
  -h, --help                   Print help
  -V, --version                Print version
```

`--path` supports shell-style expansion through `shellexpand`, so values like `~`, `~/repo`, and `$HOME/repo` work even when the caller does not expand them first.

`--interval` controls how often, in seconds, `git-watch` fetches, fast-forwards, stages changes, commits, and pushes. It defaults to `60`.

Valid log levels are:

- `trace`
- `debug`
- `info`
- `warn`
- `error`

## Home Manager module

The flake exposes a Home Manager module:

```nix
inputs.git-watch.url = "github:rhermens/git-watch";
```

Import it from your Home Manager configuration:

```nix
{
  imports = [
    inputs.git-watch.homeManagerModules.default
  ];
}
```

If you are importing from a NixOS module list that already enables Home Manager, use the wrapper module instead:

```nix
{
  imports = [
    inputs.git-watch.nixosModules.default
  ];
}
```

For nix-darwin:

```nix
{
  imports = [
    inputs.git-watch.darwinModules.default
  ];
}
```

Configure one or more services:

```nix
{
  services.git-watch = {
    notes = {
      enable = true;
      path = "~/Notes";
      logLevel = "info";
      interval = 60;
    };

    dotfiles = {
      enable = true;
      path = "~/dotfiles";
      logLevel = "debug";
      interval = 300;
    };
  };
}
```

On Linux, this creates systemd user services named:

```text
git-watch-notes.service
git-watch-dotfiles.service
```

On macOS, this creates launchd agents named:

```text
git-watch-notes
git-watch-dotfiles
```

The Home Manager `interval` option maps directly to the CLI `--interval` argument and is measured in seconds.

Disabled entries are ignored.

