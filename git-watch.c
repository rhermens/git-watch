#include <git2/annotated_commit.h>
#include <git2/checkout.h>
#include <git2/commit.h>
#include <git2/credential.h>
#include <git2/errors.h>
#include <git2/global.h>
#include <git2/index.h>
#include <git2/merge.h>
#include <git2/object.h>
#include <git2/refs.h>
#include <git2/remote.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/signature.h>
#include <git2/status.h>
#include <git2/tree.h>
#include <git2/types.h>
#include <stdio.h>
#include <sys/types.h>
#include <git2.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <sys/stat.h>

git_repository *REPOSITORY;
git_remote *REMOTE;
git_checkout_options CHECKOUT_OPTIONS = GIT_CHECKOUT_OPTIONS_INIT;
git_fetch_options FETCH_OPTIONS = GIT_FETCH_OPTIONS_INIT;
git_commit_create_options COMMIT_OPTIONS = GIT_COMMIT_CREATE_OPTIONS_INIT;
git_push_options PUSH_OPTIONS = GIT_PUSH_OPTIONS_INIT;

int cred_cb(git_cred **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload) {
    const char *homedir;

    if ((homedir = getenv("HOME")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }

    char pubkey[sizeof(homedir) + 20] = {};
    char privkey[sizeof(homedir) + 16] = {};

    sprintf(pubkey, "%s/.ssh/id_ed25519.pub", homedir);
    sprintf(privkey, "%s/.ssh/id_ed25519", homedir);

    return git_credential_ssh_key_new(out, username_from_url, pubkey, privkey, "");
}

int fetchhead_cb_merge(const char *ref_name, const char *remote_url, const git_oid *oid, unsigned int is_merge, void *payload) {
    int status = 0;
    git_annotated_commit *fetch_commit;
    git_merge_analysis_t analysis;
    git_merge_preference_t preference;
    git_object *target;
    git_reference *target_ref;
    git_reference *new_target_ref;

    if ((status = git_annotated_commit_lookup(&fetch_commit, REPOSITORY, oid)) != 0) {
        goto done;
    }

    if((status = git_repository_head(&target_ref, REPOSITORY)) != 0) {
        goto done;
    }

    if ((status = git_merge_analysis(&analysis, &preference, REPOSITORY, (const git_annotated_commit **)&fetch_commit, 1)) != 0) {
        goto done;
    }

    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE || !(analysis & GIT_MERGE_ANALYSIS_FASTFORWARD)) {
        goto done;
    }

    if ((status = git_object_lookup(&target, REPOSITORY, oid, GIT_OBJECT_COMMIT)) != 0) {
        goto done;
    }

    if ((status = git_checkout_tree(REPOSITORY, target, &CHECKOUT_OPTIONS)) != 0) {
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

int status_foreach_cb(const char *path, unsigned int status_flags, void *payload) {
    int error;
    git_index *index = (git_index *)payload;

    if (status_flags & GIT_STATUS_WT_DELETED) {
        error = git_index_remove_bypath(index, path);
        return error;
    }

    time_t curr_time;
    struct stat st;
    time(&curr_time);

    error = stat(path, &st);
    if (error != 0) {
        printf("Error %d\n", error);
        return error;
    }

    int msec = curr_time - st.st_mtim.tv_sec;
    if (msec < 60) {
        printf("File modified less than a minute ago: %s\n", path);
        return 0;
    }

    error = git_index_add_bypath(index, path);
    if (error != 0) {
        printf("Error %d: %s\n", error, git_error_last()->message);
        return error;
    }

    return 0;
}

int do_fastforward() {
    int error;
    error = git_remote_fetch(REMOTE, NULL, &FETCH_OPTIONS, NULL);
    if (error != 0) {
        const git_error *e = git_error_last();
        printf("Error %d/%d: %s\n", error, e->klass, e->message);
        return error;
    }

    git_repository_fetchhead_foreach(REPOSITORY, fetchhead_cb_merge, NULL);

    return 0;
}

int transfer_cb(const git_transfer_progress *stats, void *payload) {
    printf("Transferred %d/%d\n", stats->received_objects, stats->total_objects);
    return 0;
}

int push_transfer_cb(unsigned int i, unsigned int j, unsigned long k, void *payload) {
    printf("Pushed something\n");
    return 0;
}

int do_push() {
    int status = 0;
    git_object *head_commit;
    git_reference *head_reference;
    git_oid head_oid, tree_oid, commit_oid;
    git_index *index;
    char *ref = "refs/heads/master";
    git_strarray refspecs = { &ref, 1 };

    if ((status = git_revparse_ext(&head_commit, &head_reference, REPOSITORY, "HEAD")) != 0) {
        goto done;
    }

    if ((status = git_repository_index(&index, REPOSITORY)) != 0) {
        goto done;
    }

    git_status_foreach(REPOSITORY, status_foreach_cb, index);

    if ((status = git_index_write(index)) != 0) {
        goto done;
    }

    if ((status = git_commit_create_from_stage(&commit_oid, REPOSITORY, "Autocommit: Push commit", &COMMIT_OPTIONS)) != 0) {
        goto done;
    }

    if ((status = git_remote_push(REMOTE, &refspecs, &PUSH_OPTIONS)) != 0) {
        goto done;
    }

done:
    git_object_free(head_commit);
    git_reference_free(head_reference);
    git_index_free(index);

    return status;
}

int open_repository(char *path) {
    git_libgit2_init();
    int error;

    if ((error = git_repository_open(&REPOSITORY, path)) != 0) {
        return error;
    }

    return git_remote_lookup(&REMOTE, REPOSITORY, "origin");
}

void setup_options() {
    CHECKOUT_OPTIONS.checkout_strategy = GIT_CHECKOUT_SAFE;
    FETCH_OPTIONS.callbacks.credentials = cred_cb;
    PUSH_OPTIONS.callbacks.credentials = cred_cb;
}

int main(int args, char** argv) {
    if (args != 2) {
        printf("Usage: %s <path>", argv[0]);
        exit(1);
    }

    struct stat repository_stat;
    stat(argv[1], &repository_stat);
    if (!(repository_stat.st_mode & S_IFDIR)) {
        printf("Path does not exist");
        exit(1);
    }

    int status = 0;
    status = open_repository(argv[1]);
    setup_options();
    while (status == 0) {
        printf("Watching..\n");
        status = do_fastforward();
        status = do_push();

        git_repository_state_cleanup(REPOSITORY);
        sleep(60);
    }

    const git_error *e = git_error_last();
    printf("Error %d/%d: %s\n", status, e->klass, e->message);
    return status;
}
