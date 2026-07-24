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

fn current_branch_refspec(repo: &Repository) -> Result<String, git2::Error> {
    let branch = repo
        .head()?
        .shorthand()
        .map_err(|_| git2::Error::from_str("HEAD is not a valid branch name"))?
        .to_owned();

    Ok(format!("refs/heads/{branch}"))
}

fn push_current_branch(
    repo: &Repository,
    push_options: &mut PushOptions,
) -> Result<(), git2::Error> {
    let refspec = current_branch_refspec(repo)?;
    repo.find_remote("origin")?
        .push(&[refspec.as_str()], Some(push_options))
}

pub fn push_worktree(repo: &Repository, push_options: &mut PushOptions) -> Result<(), git2::Error> {
    let sig = repo.signature().expect("Failed to get commited signature");
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
    if status.iter().len() > 0 {
        update_index_from_status(&mut index, &status);
        index.write()?;
        let current_head = repo.find_commit(repo.head()?.target().expect("No target"))?;

        let tree = index.write_tree()?;
        if current_head.tree_id() != tree {
            repo.commit(
                Some("HEAD"),
                &sig,
                &sig,
                "Autocommit",
                &repo.find_tree(tree)?,
                &[&current_head],
            )?;
        }
    }

    push_current_branch(repo, push_options)
}
