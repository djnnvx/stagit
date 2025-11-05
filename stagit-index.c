#include <err.h>
#include <git2.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct repoinfo {
    char path[PATH_MAX + 1];
    char name[PATH_MAX + 1];
    char description[255];
    time_t last_commit_time;
};

static git_repository *repo;
static const char *relpath = "";
static char description[255] = "evil.djnn.sh";
static char *name = "";
static char category[255];

time_t get_last_commit_time(const char *repodir) {
    git_repository *repo = NULL;
    git_revwalk *w = NULL;
    git_commit *commit = NULL;
    git_oid id;
    time_t t = 0;

    if (git_repository_open_ext(&repo, repodir, GIT_REPOSITORY_OPEN_NO_SEARCH, NULL))
        return 0;
    if (git_revwalk_new(&w, repo) == 0) {
        git_revwalk_push_head(w);
        if (git_revwalk_next(&id, w) == 0) {
            if (git_commit_lookup(&commit, repo, &id) == 0) {
                const git_signature *author = git_commit_author(commit);
                if (author)
                    t = author->when.time;
                git_commit_free(commit);
            }
        }
        git_revwalk_free(w);
    }
    git_repository_free(repo);
    return t;
}

/* Handle read or write errors for a FILE * stream */
void checkfileerror(FILE *fp, const char *name, int mode) {
    if (mode == 'r' && ferror(fp))
        errx(1, "read error: %s", name);
    else if (mode == 'w' && (fflush(fp) || ferror(fp)))
        errx(1, "write error: %s", name);
}

void joinpath(char *buf, size_t bufsiz, const char *path, const char *path2) {
    int r;
    r = snprintf(buf, bufsiz, "%s%s%s", path, path[0] && path[strlen(path) - 1] != '/' ? "/" : "", path2);
    if (r < 0 || (size_t)r >= bufsiz)
        errx(1, "path truncated: '%s%s%s'", path, path[0] && path[strlen(path) - 1] != '/' ? "/" : "", path2);
}

/* Percent-encode, see RFC3986 section 2.1. */
void percentencode(FILE *fp, const char *s, size_t len) {
    static char tab[] = "0123456789ABCDEF";
    unsigned char uc;
    size_t i;
    for (i = 0; *s && i < len; s++, i++) {
        uc = *s;
        /* NOTE: do not encode '/' for paths or ",-." */
        if (uc < ',' || uc >= 127 || (uc >= ':' && uc <= '@') || uc == '[' || uc == ']') {
            putc('%', fp);
            putc(tab[(uc >> 4) & 0x0f], fp);
            putc(tab[uc & 0x0f], fp);
        } else {
            putc(uc, fp);
        }
    }
}

/* Escape characters below as HTML 2.0 / XML 1.0. */
void xmlencode(FILE *fp, const char *s, size_t len) {
    size_t i;
    for (i = 0; *s && i < len; s++, i++) {
        switch (*s) {
        case '<':
            fputs("&lt;", fp);
            break;
        case '>':
            fputs("&gt;", fp);
            break;
        case '\'':
            fputs("&#39;", fp);
            break;
        case '&':
            fputs("&amp;", fp);
            break;
        case '"':
            fputs("&quot;", fp);
            break;
        default:
            putc(*s, fp);
        }
    }
}

void printtimeshort(FILE *fp, const git_time *intime) {
    struct tm *intm;
    time_t t;
    char out[32];
    t = (time_t)intime->time;
    if (!(intm = gmtime(&t)))
        return;
    strftime(out, sizeof(out), "%Y-%m-%d", intm);
    fputs(out, fp);
}

void writeheader(FILE *fp) {
    fputs("<!DOCTYPE html>\n<meta charset=\"UTF-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>", fp);
    xmlencode(fp, description, strlen(description));
    fputs("</title>\n<meta name=\"description\" content=\"repositories\">\n"
          "<meta name=\"keywords\" content=\"git, repositories\">\n"
          "<meta name=\"author\" content=\"djnn\">\n",
          fp);
    fputs("<link rel=\"icon\" type=\"image/png\" href=\"/favicon.png\">\n"
          "<link rel=\"stylesheet\" type=\"text/css\" href=\"/style.css\">\n",
          fp);
    fputs("<div class=\"container\">\n\t<center>\n\t<table>\n\t\t<tr><td>\n"
          "<b>evil.djnn.sh ~ repositories</b>\n"
          "\t\t</td></tr>\n\t</table>\n\t</center>\n</div>\n<br>\n",
          fp);
    fputs("<div id=\"content\">\n\t<center><table id=\"index\">\n\t\t<thead>\n\t\t\t<tr><td><b>name</b></td><td><b>description</b></td><td><b>last commit</b></td></tr>\n\t\t</thead>\n\t\t<tbody>", fp);
}

void writefooter(FILE *fp) {
    fputs("\n\t\t</tbody>\n\t</table>\n</center>\n</div>\n<center>\n<br/>\n<div id=\"footer\">\n"
          "\t&copy; 2024 evil.djnn.sh &bull; generated with stagit\n"
          "</div>\n</center>",
          fp);
}

int writelog(FILE *fp) {
    git_commit *commit = NULL;
    const git_signature *author;
    git_revwalk *w = NULL;
    git_oid id;
    char *stripped_name = NULL, *p;
    int ret = 0;

    git_revwalk_new(&w, repo);
    git_revwalk_push_head(w);

    if (git_revwalk_next(&id, w) ||
        git_commit_lookup(&commit, repo, &id)) {
        ret = -1;
        goto err;
    }

    author = git_commit_author(commit);

    /* strip .git suffix */
    if (!(stripped_name = strdup(name)))
        err(1, "strdup");
    if ((p = strrchr(stripped_name, '.')))
        if (!strcmp(p, ".git"))
            *p = '\0';

    fputs("\n\t\t\t<tr class=\"item-repo\"><td><a href=\"", fp);
    percentencode(fp, stripped_name, strlen(stripped_name));
    fputs("/log.html\">", fp);
    xmlencode(fp, stripped_name, strlen(stripped_name));
    fputs("</a></td><td>", fp);
    xmlencode(fp, description, strlen(description));
    fputs("</td><td>", fp);
    if (author)
        printtimeshort(fp, &(author->when));
    fputs("</td></tr>", fp);

    git_commit_free(commit);
err:
    git_revwalk_free(w);
    free(stripped_name);

    return ret;
}

int main(int argc, char *argv[]) {
    FILE *fp;
    char path[PATH_MAX], repodirabs[PATH_MAX + 1];
    int i, ret = 0, nrepos = 0;
    struct repoinfo *repos = NULL;

    if (argc < 2) {
        fprintf(stderr, "usage: %s [repodir...]\n", argv[0]);
        return 1;
    }

    git_libgit2_init();
    for (i = 1; i <= GIT_CONFIG_LEVEL_APP; i++)
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, i, "");
    git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0);

#ifdef __OpenBSD__
    if (pledge("stdio rpath", NULL) == -1)
        err(1, "pledge");
#endif

    // Allocate space for repo info
    repos = calloc(argc - 1, sizeof(struct repoinfo));
    if (!repos)
        err(1, "calloc");

    // collect all repo info
    for (i = 1; i < argc; i++) {
        const char *repodir = argv[i];
        struct repoinfo *ri = &repos[nrepos];

        if (!realpath(repodir, repodirabs))
            err(1, "realpath");
        strncpy(ri->path, repodir, sizeof(ri->path));
        ri->path[sizeof(ri->path) - 1] = 0;
        const char *slash = strrchr(repodirabs, '/');
        strncpy(ri->name, slash ? slash + 1 : repodirabs, sizeof(ri->name));
        ri->name[sizeof(ri->name) - 1] = 0;

        // Description
        ri->description[0] = '\0';
        joinpath(path, sizeof(path), repodir, "description");
        if ((fp = fopen(path, "r"))) {
            if (!fgets(ri->description, sizeof(ri->description), fp))
                ri->description[0] = '\0';
            checkfileerror(fp, "description", 'r');
            fclose(fp);
        } else {
            joinpath(path, sizeof(path), repodir, ".git/description");
            if ((fp = fopen(path, "r"))) {
                if (!fgets(ri->description, sizeof(ri->description), fp))
                    ri->description[0] = '\0';
                checkfileerror(fp, "description", 'r');
                fclose(fp);
            }
        }

        ri->last_commit_time = get_last_commit_time(repodir);

        nrepos++;
    }

    // sort by last_commit_time DESCENDING
    int cmp_repos(const void *a, const void *b) {
        const struct repoinfo *ra = a, *rb = b;
        if (ra->last_commit_time < rb->last_commit_time)
            return 1;
        if (ra->last_commit_time > rb->last_commit_time)
            return -1;
        return strcmp(ra->name, rb->name); // fallback: alphabetical
    }
    qsort(repos, nrepos, sizeof(struct repoinfo), cmp_repos);

    // Write output
    writeheader(stdout);
    for (i = 0; i < nrepos; i++) {
        // open the repo, set globals as needed
        if (git_repository_open_ext(&repo, repos[i].path, GIT_REPOSITORY_OPEN_NO_SEARCH, NULL)) {
            fprintf(stderr, "%s: cannot open repository\n", repos[i].path);
            ret = 1;
            continue;
        }
        name = repos[i].name;
        strncpy(description, repos[i].description, sizeof(description));
        description[sizeof(description) - 1] = 0;
        writelog(stdout);
        git_repository_free(repo);
    }
    writefooter(stdout);

    git_libgit2_shutdown();
    free(repos);

    checkfileerror(stdout, "<stdout>", 'w');
    return ret;
}
