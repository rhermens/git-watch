mod fastforward;
mod ident;
mod push;

use std::{path::PathBuf, sync::mpsc, time::Duration};

use clap::Parser;
use git2::{FetchOptions, PushOptions, RemoteCallbacks, Repository};
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

    #[arg(long, default_value_t = 60)]
    interval: u64,
}

#[derive(Debug)]
enum EventKind {
    Tick(()),
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

    start_interval(tx.clone(), args.interval);

    for e in rx {
        tracing::trace!("Received event: {:?}", e);
        match e {
            EventKind::Tick(_) => {
                tracing::trace!("Processing tick");
                if let Err(e) = fast_forward(&repo, &mut fetch_options) {
                    tracing::error!("error fast-forwarding: {}", e);
                }

                if let Err(err) = update_index(&repo) {
                    tracing::error!("Error during commit: {:?}", err);
                }

                if let Err(e) = push_worktree(&repo, &mut push_options) {
                    tracing::error!("error pushing: {}", e);
                }
            }
        }
    }
}

fn start_interval(tx: mpsc::Sender<EventKind>, secs: u64) {
    std::thread::spawn(move || {
        loop {
            std::thread::sleep(Duration::from_secs(secs));
            tx.send(EventKind::Tick(()))
                .expect("Failed to send interval tick");
        }
    });
}
