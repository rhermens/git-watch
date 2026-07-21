use std::path::Path;

use git2::{Index, PushOptions, Repository, Status, StatusOptions, Statuses};

fn update_index_from_status(index: &mut Index, status: &Statuses) {
    for entry in status.iter() {
        tracing::info!("{:?}", entry.path());
        if let Err(err) = match entry.status() {
            Status::WT_NEW | Status::WT_MODIFIED | Status::INDEX_MODIFIED | Status::INDEX_NEW => {
                index.add_path(Path::new(entry.path().expect("Failed to get status path")))
            }
            Status::WT_DELETED | Status::INDEX_DELETED => {
                index.remove_path(Path::new(entry.path().expect("Failed to get status path")))
            }
            _ => continue,
        } {
            tracing::error!(
                "Failed to add/remove path: {:?}, error: {:?}",
                entry.path(),
                err
            );
            continue;
        }
    }
}

pub fn update_index(repo: &Repository) -> Result<(), git2::Error> {
    let mut index = repo.index()?;
    let status = repo.statuses(Some(
        StatusOptions::new()
            .include_untracked(true)
            .recurse_untracked_dirs(true)
            .include_ignored(false)
            .include_unmodified(false)
            .include_unreadable(false),
    ))?;

    tracing::info!("changes: {:?}", status.iter().len());
    if status.iter().len() == 0 {
        return Ok(());
    }

    update_index_from_status(&mut index, &status);
    index.write()
}

pub fn push_worktree(repo: &Repository, push_options: &mut PushOptions) -> Result<(), git2::Error> {
    let sig = repo.signature().expect("Failed to get commited signature");
    let mut index = repo.index()?;
    let current_head = repo.find_commit(repo.head()?.target().expect("No target"))?;
    let _ = repo.commit(
        Some("HEAD"),
        &sig,
        &sig,
        "Autocommit",
        &repo.find_tree(index.write_tree()?)?,
        &[&current_head],
    )?;
    repo.find_remote("origin")?
        .push(&["refs/heads/master"], Some(push_options))
}
