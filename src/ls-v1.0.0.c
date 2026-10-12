/*
* Programming Assignment 02: ls
* Version 1.5.0 - colorized output based on file type
* Usage:
*       $ ls
*       $ ls -l
*       $ ls -x
*       $ ls -l /home /etc
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

extern int errno;

/* ANSI escape codes for colors (\033 is the ESC character) */
#define COLOR_RESET    "\033[0m"
#define COLOR_BLUE     "\033[0;34m"    /* directories */
#define COLOR_GREEN    "\033[0;32m"    /* executables */
#define COLOR_RED      "\033[0;31m"    /* archives: .tar .gz .zip */
#define COLOR_PINK     "\033[1;35m"    /* symbolic links */
#define COLOR_REVERSE  "\033[7m"       /* special files: reverse video */

/* The three ways the program can display files */
typedef enum
{
    MODE_DEFAULT,      /* columns, down then across */
    MODE_LONG,         /* -l */
    MODE_HORIZONTAL    /* -x */
} display_mode_t;

void do_ls(const char *dir, display_mode_t mode);
int read_names(const char *dir, char ***names_out, int *count_out, int *maxlen_out);
int compare_names(const void *a, const void *b);
void free_names(char **names, int count);
void display_long(const char *dir, char **names, int count);
void display_columns(const char *dir, char **names, int count, int maxlen);
void display_horizontal(const char *dir, char **names, int count, int maxlen);
const char *get_color(const char *name, mode_t mode);
void print_colored(const char *name, mode_t mode);
void print_name_at(const char *dir, const char *name);
int get_terminal_width(void);
void mode_to_string(mode_t mode, char *str);
const char *user_name(uid_t uid, char *buf, size_t n);
const char *group_name(gid_t gid, char *buf, size_t n);

int main(int argc, char *argv[])
{
    int opt;
    display_mode_t mode = MODE_DEFAULT;

    /* getopt scans the command line for options such as -l */
    while ((opt = getopt(argc, argv, "lx")) != -1)
    {
        switch (opt)
        {
        case 'l':
            mode = MODE_LONG;
            break;
        case 'x':
            mode = MODE_HORIZONTAL;
            break;
        default:
            fprintf(stderr, "Usage: %s [-l | -x] [directory...]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* optind is the index of the first non-option argument */
    if (optind == argc)
    {
        do_ls(".", mode);
    }
    else
    {
        for (int i = optind; i < argc; i++)
        {
            printf("Directory listing of %s : \n", argv[i]);
            do_ls(argv[i], mode);
            puts("");
        }
    }
    return 0;
}

/* Read all names, sort them, then hand them to the right display function */
void do_ls(const char *dir, display_mode_t mode)
{
    char **names = NULL;
    int count = 0;
    int maxlen = 0;

    if (read_names(dir, &names, &count, &maxlen) == -1)
        return;

    /* Sort alphabetically BEFORE any display function is called */
    qsort(names, count, sizeof(char *), compare_names);

    /* Choose the display function based on the mode flag */
    if (mode == MODE_LONG)
    {
        display_long(dir, names, count);
    }
    else if (count > 0)
    {
        if (mode == MODE_HORIZONTAL)
            display_horizontal(dir, names, count, maxlen);
        else
            display_columns(dir, names, count, maxlen);
    }

    free_names(names, count);
}

/* Read every (non-hidden) entry of a directory into a dynamic array.
   Returns 0 on success, -1 if the directory cannot be opened. */
int read_names(const char *dir, char ***names_out, int *count_out, int *maxlen_out)
{
    struct dirent *entry;
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return -1;
    }

    char **names = NULL;   /* dynamic array of filenames */
    int count = 0;         /* how many names stored */
    int capacity = 0;      /* how many slots allocated */
    int maxlen = 0;        /* length of the longest filename */

    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        /* Grow the array when it is full (double the size) */
        if (count == capacity)
        {
            capacity = (capacity == 0) ? 16 : capacity * 2;
            char **tmp = realloc(names, capacity * sizeof(char *));
            if (tmp == NULL)
            {
                perror("realloc failed");
                break;
            }
            names = tmp;
        }

        names[count] = strdup(entry->d_name);
        if (names[count] == NULL)
        {
            perror("strdup failed");
            break;
        }
        int len = (int)strlen(names[count]);
        if (len > maxlen)
            maxlen = len;
        count++;
    }

    if (errno != 0)
    {
        perror("readdir failed");
    }
    closedir(dp);

    *names_out = names;
    *count_out = count;
    *maxlen_out = maxlen;
    return 0;
}

/* Comparison function for qsort: qsort passes pointers to the array
   elements, and each element is a char *, so each argument is a char ** */
int compare_names(const void *a, const void *b)
{
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;
    return strcmp(s1, s2);
}

/* Free every string, then the array itself */
void free_names(char **names, int count)
{
    for (int i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

/* Decide which color a file gets, based on its st_mode and its name.
   Returns NULL when the file should keep the default color. */
const char *get_color(const char *name, mode_t mode)
{
    if (S_ISLNK(mode))
        return COLOR_PINK;
    if (S_ISDIR(mode))
        return COLOR_BLUE;
    if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISSOCK(mode) || S_ISFIFO(mode))
        return COLOR_REVERSE;
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        return COLOR_GREEN;

    /* Archives: check the filename extension */
    const char *dot = strrchr(name, '.');
    if (dot != NULL &&
        (strcmp(dot, ".tar") == 0 || strcmp(dot, ".gz") == 0 ||
         strcmp(dot, ".tgz") == 0 || strcmp(dot, ".zip") == 0))
        return COLOR_RED;

    return NULL;
}

/* Print a filename in its color (only when output goes to a terminal) */
void print_colored(const char *name, mode_t mode)
{
    const char *color = NULL;
    if (isatty(STDOUT_FILENO))
        color = get_color(name, mode);

    if (color != NULL)
        printf("%s%s%s", color, name, COLOR_RESET);
    else
        printf("%s", name);
}

/* lstat() a file inside dir, then print its name in the right color */
void print_name_at(const char *dir, const char *name)
{
    char path[PATH_MAX];
    struct stat st;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (lstat(path, &st) == -1)
        printf("%s", name);              /* cannot stat: plain name */
    else
        print_colored(name, st.st_mode);
}

/* Get the terminal width using ioctl; fall back to 80 columns */
int get_terminal_width(void)
{
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1 || w.ws_col == 0)
        return 80;
    return w.ws_col;
}

/* Print names in columns, "down then across" */
void display_columns(const char *dir, char **names, int count, int maxlen)
{
    int term_width = get_terminal_width();
    int col_width = maxlen + 2;          /* longest name + 2 spaces */
    int cols = term_width / col_width;   /* how many columns fit */
    if (cols < 1)
        cols = 1;
    if (cols > count)
        cols = count;
    int rows = (count + cols - 1) / cols; /* ceiling of count / cols */

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            int idx = r + c * rows;      /* down-then-across index */
            if (idx >= count)
                break;
            int next = idx + rows;       /* next item on this row */
            print_name_at(dir, names[idx]);
            /* Pad with spaces by hand: color codes would break %-*s */
            if (c + 1 < cols && next < count)
                printf("%*s", col_width - (int)strlen(names[idx]), "");
        }
        printf("\n");
    }
}

/* Print names left to right, wrapping when the line is full (-x) */
void display_horizontal(const char *dir, char **names, int count, int maxlen)
{
    int term_width = get_terminal_width();
    int col_width = maxlen + 2;   /* longest name + 2 spaces */
    int pos = 0;                  /* current horizontal position on the line */

    for (int i = 0; i < count; i++)
    {
        /* If this item would not fit on the current line, start a new line */
        if (pos > 0 && pos + col_width > term_width)
        {
            printf("\n");
            pos = 0;
        }
        print_name_at(dir, names[i]);
        /* Pad with spaces by hand: color codes would break %-*s */
        printf("%*s", col_width - (int)strlen(names[i]), "");
        pos += col_width;
    }
    printf("\n");
}

/* Long listing: like "ls -l". Two passes so the columns line up. */
void display_long(const char *dir, char **names, int count)
{
    struct stat st;
    char path[PATH_MAX];
    char buf[64];
    char ubuf[32], gbuf[32];
    int w_link = 0, w_user = 0, w_group = 0, w_size = 0;
    long long total_blocks = 0;

    /* Pass 1: find the widest value in each column and the block total */
    for (int i = 0; i < count; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (lstat(path, &st) == -1)
        {
            perror(path);
            continue;
        }
        total_blocks += st.st_blocks;

        int n = snprintf(buf, sizeof(buf), "%lu", (unsigned long)st.st_nlink);
        if (n > w_link)
            w_link = n;
        n = (int)strlen(user_name(st.st_uid, ubuf, sizeof(ubuf)));
        if (n > w_user)
            w_user = n;
        n = (int)strlen(group_name(st.st_gid, gbuf, sizeof(gbuf)));
        if (n > w_group)
            w_group = n;
        n = snprintf(buf, sizeof(buf), "%lld", (long long)st.st_size);
        if (n > w_size)
            w_size = n;
    }

    /* st_blocks counts 512-byte blocks, ls prints 1K blocks */
    printf("total %lld\n", total_blocks / 2);

    /* Pass 2: print every entry (already sorted) */
    for (int i = 0; i < count; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (lstat(path, &st) == -1)
            continue;

        char perms[11];
        mode_to_string(st.st_mode, perms);

        /* ctime() gives "Wed Jun 30 21:49:08 1993\n"; keep "Jun 30 21:49" */
        char timestr[16];
        char *t = ctime(&st.st_mtime);
        snprintf(timestr, sizeof(timestr), "%.12s", t + 4);

        printf("%s %*lu %-*s %-*s %*lld %s ",
               perms,
               w_link, (unsigned long)st.st_nlink,
               w_user, user_name(st.st_uid, ubuf, sizeof(ubuf)),
               w_group, group_name(st.st_gid, gbuf, sizeof(gbuf)),
               w_size, (long long)st.st_size,
               timestr);
        print_colored(names[i], st.st_mode);

        /* For symbolic links, show where they point */
        if (S_ISLNK(st.st_mode))
        {
            char target[PATH_MAX];
            ssize_t len = readlink(path, target, sizeof(target) - 1);
            if (len != -1)
            {
                target[len] = '\0';
                printf(" -> %s", target);
            }
        }
        printf("\n");
    }
}

/* Convert st_mode into a string like "drwxr-xr-x" */
void mode_to_string(mode_t mode, char *str)
{
    /* Character 0: file type */
    if (S_ISDIR(mode))
        str[0] = 'd';
    else if (S_ISLNK(mode))
        str[0] = 'l';
    else if (S_ISCHR(mode))
        str[0] = 'c';
    else if (S_ISBLK(mode))
        str[0] = 'b';
    else if (S_ISFIFO(mode))
        str[0] = 'p';
    else if (S_ISSOCK(mode))
        str[0] = 's';
    else
        str[0] = '-';

    /* Owner */
    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    if (mode & S_ISUID)
        str[3] = (mode & S_IXUSR) ? 's' : 'S';
    else
        str[3] = (mode & S_IXUSR) ? 'x' : '-';

    /* Group */
    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    if (mode & S_ISGID)
        str[6] = (mode & S_IXGRP) ? 's' : 'S';
    else
        str[6] = (mode & S_IXGRP) ? 'x' : '-';

    /* Others */
    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    if (mode & S_ISVTX)
        str[9] = (mode & S_IXOTH) ? 't' : 'T';
    else
        str[9] = (mode & S_IXOTH) ? 'x' : '-';

    str[10] = '\0';
}

/* Turn a user ID into a name; fall back to the number if unknown */
const char *user_name(uid_t uid, char *buf, size_t n)
{
    struct passwd *pw = getpwuid(uid);
    if (pw != NULL)
        return pw->pw_name;
    snprintf(buf, n, "%u", (unsigned)uid);
    return buf;
}

/* Turn a group ID into a name; fall back to the number if unknown */
const char *group_name(gid_t gid, char *buf, size_t n)
{
    struct group *gr = getgrgid(gid);
    if (gr != NULL)
        return gr->gr_name;
    snprintf(buf, n, "%u", (unsigned)gid);
    return buf;
}
