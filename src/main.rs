mod fastforward;
mod ident;
mod push;

use std::{path::PathBuf, sync::mpsc, time::Duration};

use clap::Parser;
use git2::{FetchOptions, PushOptions, RemoteCallbacks, Repository};
use notify::RecursiveMode;
use notify_debouncer_full::new_debouncer;
use tracing::Level;

use crate::{
    fastforward::fast_forward,
    ident::credentials_callback,
    push::{push_worktree, update_index},
};

#[derive(Parser, Debug)]
#[command(version)]
struct Args {
    #[arg(short, long, value_parser=path_expand)]
    path: PathBuf,

    #[arg(long, default_value_t = Level::INFO)]
    log_level: Level,
}

#[derive(Debug)]
enum EventKind {
    Tick(()),
    Fs(()),
}

fn path_expand(value: &str) -> Result<PathBuf, String> {
    Ok(PathBuf::from(
        shellexpand::full(value)
            .map_err(|_| "Invalid path")?
            .into_owned(),
    ))
}

fn main() {
    let args = Args::parse();
    tracing_subscriber::fmt()
        .with_max_level(args.log_level)
        .init();

    let (tx, rx) = mpsc::channel();
    let repo = Repository::open(&args.path).expect("Failed to open repository");

    let mut push_options = PushOptions::new();
    let mut push_callbacks = RemoteCallbacks::default();
    push_callbacks.credentials(credentials_callback);
    push_options.remote_callbacks(push_callbacks);

    let mut fetch_options = FetchOptions::new();
    let mut fetch_callbacks = RemoteCallbacks::default();
    fetch_callbacks.credentials(credentials_callback);
    fetch_options.remote_callbacks(fetch_callbacks);

    start_interval(tx.clone());
    start_fs_watch(args.path, tx.clone());

    for e in rx {
        tracing::trace!("Received event: {:?}", e);
        match e {
            EventKind::Tick(_) => {
                tracing::trace!("Processing tick");
                if let Err(e) = fast_forward(&repo, &mut fetch_options) {
                    tracing::error!("error fast-forwarding: {}", e);
                }

                if let Err(e) = push_worktree(&repo, &mut push_options) {
                    tracing::error!("error pushing: {}", e);
                }
            }
            EventKind::Fs(_) => {
                tracing::trace!("Adding changes");
                if let Err(err) = update_index(&repo) {
                    tracing::error!("Error during commit: {:?}", err);
                }
            }
        }
    }
}

fn start_interval(tx: mpsc::Sender<EventKind>) {
    std::thread::spawn(move || {
        loop {
            std::thread::sleep(Duration::from_secs(60));
            tx.send(EventKind::Tick(()))
                .expect("Failed to send interval tick");
        }
    });
}

fn start_fs_watch(path: PathBuf, tx: mpsc::Sender<EventKind>) {
    std::thread::spawn(move || {
        let repo = Repository::open(&path).expect("Failed to open repository");
        let (dtx, rx) = mpsc::channel();

        let mut debouncer =
            new_debouncer(Duration::from_secs(30), None, dtx).expect("Failed to create debouncer");

        debouncer
            .watch(&path, RecursiveMode::Recursive)
            .expect("failed to watch");

        for result in rx {
            if result
                .expect("Failed to read events")
                .iter()
                .filter(|e| e.kind.is_remove() || e.kind.is_create() || e.kind.is_modify())
                .filter(|e| {
                    e.paths
                        .iter()
                        .any(|p| !repo.is_path_ignored(p).expect("Failed to check ignore"))
                })
                .count()
                == 0
            {
                tracing::debug!("No relevant file system changes detected, skipping commit.");
                continue;
            }

            tracing::debug!("Fs change, broadcasting event");
            tx.send(EventKind::Fs(()))
                .expect("Failed to send file system event");
        }
    });
}
