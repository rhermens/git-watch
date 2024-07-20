#include <git2/commit.h>
#include <git2/index.h>
#include <git2/remote.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/status.h>
#include <git2/strarray.h>
#include <git2/types.h>
#include <sys/stat.h>
#include "push.h"

int status_foreach_cb(const char *path, unsigned int status_flags, void *payload) {
    status_cb_payload *pl = (status_cb_payload *)payload;

    if (status_flags & GIT_STATUS_WT_DELETED) {
        pl->changes++;
        return git_index_remove_bypath(pl->index, path);
    }

    time_t curr_time;
    struct stat st;
    time(&curr_time);

    if (stat(path, &st) != 0) {
        return 0;
    }

    int msec = curr_time - st.st_mtime;
    if (msec < 60) {
        return 0;
    }

    pl->changes++;
    return git_index_add_bypath(pl->index, path);
}

int do_push(git_repository *repository, git_remote *remote, git_commit_create_options *commit_options, git_push_options *push_options) {
    int status = 0;
    git_object *head_commit;
    git_reference *head_reference;
    git_oid commit_oid;
    status_cb_payload cb_payload = { .index = NULL, .changes = 0 };
    char *ref = "refs/heads/master";
    git_strarray refspecs = { &ref, 1 };

    if ((status = git_revparse_ext(&head_commit, &head_reference, repository, "HEAD")) != 0) {
        goto done;
    }

    if ((status = git_repository_index(&cb_payload.index, repository)) != 0) {
        goto done;
    }

    if ((status = git_status_foreach(repository, status_foreach_cb, &cb_payload)) != 0) {
        goto done;
    }

    if (cb_payload.changes == 0) {
        goto done;
    }

    if ((status = git_index_write(cb_payload.index)) != 0) {
        goto done;
    }

    if ((status = git_commit_create_from_stage(&commit_oid, repository, "Autocommit: Push commit", commit_options)) != 0) {
        goto done;
    }

    if ((status = git_remote_push(remote, &refspecs, push_options)) != 0) {
        goto done;
    }

done:
    git_object_free(head_commit);
    git_reference_free(head_reference);
    git_index_free(cb_payload.index);

    return status;
}
