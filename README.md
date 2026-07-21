# git-fsnotify

`git-fsnotify` watches a Git worktree, stages filesystem changes as they happen, and periodically synchronizes with `origin`.

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
git-fsnotify --path ~/Notes --log-level info
```

CLI options:

```text
Usage: git-fsnotify [OPTIONS] --path <PATH>

Options:
  -p, --path <PATH>
      --log-level <LOG_LEVEL>  [default: INFO]
  -h, --help                   Print help
  -V, --version                Print version
```

`--path` supports shell-style expansion through `shellexpand`, so values like `~`, `~/repo`, and `$HOME/repo` work even when the caller does not expand them first.

Valid log levels are:

- `trace`
- `debug`
- `info`
- `warn`
- `error`

## Home Manager module

The flake exposes a Home Manager module:

```nix
inputs.git-fsnotify.url = "github:rhermens/git-fsnotify";
```

Import it from your Home Manager configuration:

```nix
{
  imports = [
    inputs.git-fsnotify.homeManagerModules.${pkgs.stdenv.hostPlatform.system}.git-fsnotify
  ];
}
```

Configure one or more services:

```nix
{
  services.git-fsnotify = {
    notes = {
      enable = true;
      path = /home/roy/Notes;
      logLevel = "info";
    };

    dotfiles = {
      enable = true;
      path = /home/roy/dotfiles;
      logLevel = "debug";
    };
  };
}
```

On Linux, this creates systemd user services named:

```text
git-fsnotify-notes.service
git-fsnotify-dotfiles.service
```

On macOS, this creates launchd agents named:

```text
git-fsnotify-notes
git-fsnotify-dotfiles
```

Disabled entries are ignored.

