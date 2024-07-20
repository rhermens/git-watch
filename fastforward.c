#include <git2/checkout.h>
#include <git2/merge.h>
#include <git2/remote.h>
#include <git2/types.h>
#include "fastforward.h"

int fetchhead_cb(const char *ref_name, const char *remote_url, const git_oid *oid, unsigned int is_merge, void *payload) {
    int status = 0;
    fastforward_cb_payload *ff_payload = (fastforward_cb_payload *)payload;
    git_annotated_commit *fetch_commit = NULL;
    git_merge_analysis_t analysis;
    git_merge_preference_t preference;
    git_object *target = NULL;
    git_reference *target_ref = NULL;
    git_reference *new_target_ref = NULL;

    if ((status = git_annotated_commit_lookup(&fetch_commit, ff_payload->repository, oid)) != 0) {
        goto done;
    }

    if((status = git_repository_head(&target_ref, ff_payload->repository)) != 0) {
        goto done;
    }

    if ((status = git_merge_analysis(&analysis, &preference, ff_payload->repository, (const git_annotated_commit **)&fetch_commit, 1)) != 0) {
        goto done;
    }

    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE || !(analysis & GIT_MERGE_ANALYSIS_FASTFORWARD)) {
        goto done;
    }

    if ((status = git_object_lookup(&target, ff_payload->repository, oid, GIT_OBJECT_COMMIT)) != 0) {
        goto done;
    }

    if ((status = git_checkout_tree(ff_payload->repository, target, ff_payload->checkout_options)) != 0) {
        goto done;
    }

    if ((status = git_reference_set_target(&new_target_ref, target_ref, oid, NULL)) != 0) {
        goto done;
    }

done:
    git_annotated_commit_free(fetch_commit);
    git_reference_free(target_ref);
    git_reference_free(new_target_ref);
    git_object_free(target);
    return status;
}

int do_fastforward(git_repository *repository, git_remote *remote, git_fetch_options *fetch_options, git_checkout_options *checkout_options) {
    int error;
    fastforward_cb_payload ff_payload = { .repository = repository, .checkout_options = checkout_options };

    if ((error = git_remote_fetch(remote, NULL, fetch_options, NULL)) != 0) {
        return error;
    }

    return git_repository_fetchhead_foreach(repository, fetchhead_cb, &ff_payload);
}


