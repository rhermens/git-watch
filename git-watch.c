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
#include <stdarg.h>
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

typedef struct status_cb_payload {
    git_index *index;
    unsigned int changes;
} status_cb_payload;

void verbose_print(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

int cred_cb(git_cred **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload) {
    const char *homedir;

    if ((homedir = getenv("HOME")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }

    char pubkey[sizeof(homedir) + 20];
    char privkey[sizeof(homedir) + 16];

    sprintf(pubkey, "%s/.ssh/id_ed25519.pub", homedir);
    sprintf(privkey, "%s/.ssh/id_ed25519", homedir);

    return git_credential_ssh_key_new(out, username_from_url, pubkey, privkey, "");
}

int fetchhead_cb_merge(const char *ref_name, const char *remote_url, const git_oid *oid, unsigned int is_merge, void *payload) {
    int status = 0;
    git_annotated_commit *fetch_commit = NULL;
    git_merge_analysis_t analysis;
    git_merge_preference_t preference;
    git_object *target = NULL;
    git_reference *target_ref = NULL;
    git_reference *new_target_ref = NULL;

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
    status_cb_payload *pl = (status_cb_payload *)payload;

    if (status_flags & GIT_STATUS_WT_DELETED) {
        pl->changes++;
        return git_index_remove_bypath(pl->index, path);
    }

    time_t curr_time;
    struct stat st;
    time(&curr_time);

    if (stat(path, &st) != 0) {
        verbose_print("Error statting file %d\n", path);
        return 0;
    }

    int msec = curr_time - st.st_mtime;
    if (msec < 60) {
        verbose_print("File modified less than a minute ago: %s\n", path);
        return 0;
    }

    pl->changes++;
    return git_index_add_bypath(pl->index, path);
}

int do_fastforward() {
    int error;

    if ((error = git_remote_fetch(REMOTE, NULL, &FETCH_OPTIONS, NULL)) != 0) {
        return error;
    }

    return git_repository_fetchhead_foreach(REPOSITORY, fetchhead_cb_merge, NULL);
}

int do_push() {
    int status = 0;
    git_object *head_commit;
    git_reference *head_reference;
    git_oid commit_oid;
    status_cb_payload cb_payload = { .index = NULL, .changes = 0 };
    char *ref = "refs/heads/master";
    git_strarray refspecs = { &ref, 1 };

    if ((status = git_revparse_ext(&head_commit, &head_reference, REPOSITORY, "HEAD")) != 0) {
        goto done;
    }

    if ((status = git_repository_index(&cb_payload.index, REPOSITORY)) != 0) {
        goto done;
    }

    if ((status = git_status_foreach(REPOSITORY, status_foreach_cb, &cb_payload)) != 0) {
        goto done;
    }

    if (cb_payload.changes == 0) {
        verbose_print("No changes..\n");
        goto done;
    }

    if ((status = git_index_write(cb_payload.index)) != 0) {
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
    git_index_free(cb_payload.index);

    return status;
}

int open_repository(char *path) {
    git_libgit2_init();
    int error;

    if ((error = git_repository_open(&REPOSITORY, path)) != 0) {
        return error;
    }

    chdir(path);

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
    if (!(repository_stat.st_mode & S_ISDIR(repository_stat.st_mode))) {
        printf("Path does not exist");
        exit(1);
    }

    int status = 0;
    status = open_repository(argv[1]);
    setup_options();
    while (status == 0) {
        verbose_print("Watching..\n");

        verbose_print("Fastforward..\n");
        if ((status = do_fastforward()) != 0) {
            break;
        }

        verbose_print("Push..\n");
        if ((status = do_push()) != 0) {
            break;
        }

        if ((status = git_repository_state_cleanup(REPOSITORY)) != 0) {
            break;
        }

        sleep(60);
    }


    fprintf(stderr, "Error %d: %s\n", status, git_error_last()->message);

    git_repository_free(REPOSITORY);
    git_remote_free(REMOTE);
    git_libgit2_shutdown();

    return status;
}
