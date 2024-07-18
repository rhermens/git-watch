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

int cred_cb(git_cred **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload) {
    printf("Logging in\n");
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
    int error;
    const git_error *e;
    git_annotated_commit *fetch_commit;
    git_merge_analysis_t analysis;
    git_merge_preference_t preference;
    git_object *target;
    git_reference *target_ref;
    git_reference *new_target_ref;
    const git_annotated_commit **their_head;

    error = git_annotated_commit_lookup(&fetch_commit, REPOSITORY, oid);
    if (error != 0) {
        e = git_error_last();
        printf("Error %d/%d: %s\n", error, e->klass, e->message);
        return error;
    }

    error = git_repository_head(&target_ref, REPOSITORY);
    if (error != 0) {
        e = git_error_last();
        printf("Error %d/%d: %s\n", error, e->klass, e->message);
        return error;
    }

    their_head = (const git_annotated_commit **)&fetch_commit;
    error = git_merge_analysis(&analysis, &preference, REPOSITORY, their_head, 1);
    if (error != 0) {
        e = git_error_last();
        printf("Error %d/%d: %s\n", error, e->klass, e->message);
        return error;
    }

    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
        printf("Already up to date\n");
        return 0;
    }

    if (!(analysis & GIT_MERGE_ANALYSIS_FASTFORWARD)) {
        printf("Fast-forward impossible\n");
        return 1;
    }

    printf("Fast-forwarding..");
    error = git_object_lookup(&target, REPOSITORY, oid, GIT_OBJECT_COMMIT);
    if (error != 0) {
        e = git_error_last();
        printf("Error %d/%d: %s\n", error, e->klass, e->message);
        return error;
    }

    error = git_checkout_tree(REPOSITORY, target, &CHECKOUT_OPTIONS);
    if (error != 0) {
        e = git_error_last();
        printf("Error %d/%d: %s\n", error, e->klass, e->message);
        return error;
    }

    error = git_reference_set_target(&new_target_ref, target_ref, oid, NULL);
    if (error != 0) {
        e = git_error_last();
        printf("Error %d/%d: %s\n", error, e->klass, e->message);
        return error;
    }

    git_reference_free(target_ref);
    git_reference_free(new_target_ref);
    git_object_free(target);

    return 0;
}

int status_cb(const char *path, unsigned int status_flags, void *payload) {
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
    printf("Doing push maybe..\n");
    int error;
    git_object *head_commit;
    git_reference *head_reference;
    git_oid head_oid, tree_oid, commit_oid;
    git_index *index;

    error = git_revparse_ext(&head_commit, &head_reference, REPOSITORY, "HEAD");
    if (error != 0) {
        printf("Error %d: %s\n", error, git_error_last()->message);
        return error;
    }

    error = git_repository_index(&index, REPOSITORY);
    if (error != 0) {
        printf("Error %d: %s\n", error, git_error_last()->message);
        return error;
    }

    git_status_foreach(REPOSITORY, status_cb, index);

    error = git_index_write(index);
    if (error != 0) {
        printf("Oof, Error %d: %s\n", error, git_error_last()->message);
        return error;
    }

    git_commit_create_options commit_opts = GIT_COMMIT_CREATE_OPTIONS_INIT;
    error = git_commit_create_from_stage(&commit_oid, REPOSITORY, "Autocommit: Push commit", &commit_opts);
    if (error != 0) {
        printf("Error %d: %s\n", error, git_error_last()->message);
        return error;
    }

    git_push_options push_options = GIT_PUSH_OPTIONS_INIT;
    push_options.callbacks.credentials = cred_cb;
    char *ref = "refs/heads/master";
    git_strarray refspecs = { &ref, 1 };
    error = git_remote_push(REMOTE, &refspecs, &push_options);

    if (error != 0) {
        printf("Error %d: %s\n", error, git_error_last()->message);
        return error;
    }

    git_index_free(index);

    return 0;
}

int main(int args, char** argv) {
    if (args != 2) {
        printf("Usage: autosync <path>");
        return 1;
    }

    struct stat repository_stat;
    stat(argv[1], &repository_stat);
    if (!(repository_stat.st_mode & S_IFDIR)) {
        printf("Path does not exist");
        return 1;
    }
    chdir(argv[1]);

    git_libgit2_init();
    FETCH_OPTIONS.callbacks.credentials = cred_cb;
    CHECKOUT_OPTIONS.checkout_strategy = GIT_CHECKOUT_SAFE;
    int error;

    error = git_repository_open(&REPOSITORY, argv[1]);
    if (error != 0) {
        const git_error *e = git_error_last();
        printf("Error %d/%d: %s\n", error, e->klass, e->message);
        exit(error);
    }

    error = git_remote_lookup(&REMOTE, REPOSITORY, "origin");
    if (error != 0) {
        const git_error *e = git_error_last();
        printf("Error %d/%d: %s\n", error, e->klass, e->message);
        exit(error);
    }

    while (1) {
        printf("Watching..\n");
        do_fastforward();
        do_push();
        git_repository_state_cleanup(REPOSITORY);
        sleep(60);
    }
}
