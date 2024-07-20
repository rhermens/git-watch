#include <git2/common.h>
#include <git2/credential.h>
#include <git2/errors.h>
#include <git2/global.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <sys/stat.h>

#include "push.h"
#include "fastforward.h"

void verbose_print(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

int cred_cb(git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload) {
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

int open_repository(git_repository **repository, git_remote **remote, char *path) {
    int error;

    if ((error = git_repository_open(repository, path)) != 0) {
        return error;
    }

    chdir(path);

    return git_remote_lookup(remote, *repository, "origin");
}

void setup_options(git_checkout_options *checkout_options, git_fetch_options *fetch_options, git_push_options *push_options) {
    checkout_options->checkout_strategy = GIT_CHECKOUT_SAFE;
    fetch_options->callbacks.credentials = cred_cb;
    push_options->callbacks.credentials = cred_cb;
}

int main(int args, char** argv) {
    if (args != 2) {
        int maj, min, rev;
        git_libgit2_version(&maj, &min, &rev);
        printf("Usage: %s <path>\n", argv[0]);
        printf("Libgit version %i.%i.%i\n", maj, min, rev);
        exit(1);
    }

    struct stat repository_stat;
    stat(argv[1], &repository_stat);
    if (!(repository_stat.st_mode & S_ISDIR(repository_stat.st_mode))) {
        printf("Path does not exist");
        exit(1);
    }

    int status = 0;
    git_repository *repository;
    git_remote *remote;

    git_checkout_options checkout_options = GIT_CHECKOUT_OPTIONS_INIT;
    git_fetch_options fetch_options = GIT_FETCH_OPTIONS_INIT;
    git_commit_create_options commit_options = GIT_COMMIT_CREATE_OPTIONS_INIT;
    git_push_options push_options = GIT_PUSH_OPTIONS_INIT;

    git_libgit2_init();

    status = open_repository(&repository, &remote, argv[1]);
    setup_options(&checkout_options, &fetch_options, &push_options);

    while (status == 0) {
        verbose_print("Watching..\n");

        verbose_print("Fastforward..\n");
        if ((status = do_fastforward(repository, remote, &fetch_options, &checkout_options)) != 0) {
            break;
        }

        verbose_print("Push..\n");
        if ((status = do_push(repository, remote, &commit_options, &push_options)) != 0) {
            break;
        }

        if ((status = git_repository_state_cleanup(repository)) != 0) {
            break;
        }

        sleep(60);
    }


    fprintf(stderr, "Error %d: %s\n", status, git_error_last()->message);

    git_repository_free(repository);
    git_remote_free(remote);
    git_libgit2_shutdown();

    return status;
}
