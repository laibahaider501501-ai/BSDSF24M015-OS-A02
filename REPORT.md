# REPORT.md: Programming Assignment 02 (Re-implementing `ls`)

**Name:** LAIBA
**Roll No:** BSDSF24M015

## Versions Built

| Version | Feature | Branch |
|---|---|---|
| v1.1.0 | Long listing (`-l`) | feature-long-listing-v1.1.0 |
| v1.2.0 | Column display (down then across) | feature-column-display-v1.2.0 |
| v1.3.0 | Horizontal display (`-x`) | feature-horizontal-display-v1.3.0 |
| v1.4.0 | Alphabetical sort | feature-alphabetical-sort-v1.4.0 |
| v1.5.0 | Colorized output | feature-colorized-output-v1.5.0 |
| v1.6.0 | Recursive listing (`-R`) | feature-recursive-listing-v1.6.0 |

---

## Feature 2 (v1.1.0): Long Listing Format

### Q1. What is the crucial difference between `stat()` and `lstat()`? When is `lstat()` more appropriate in `ls`?

`stat()` follows symbolic links: if the path is a symlink, it returns information about the file the link points to. `lstat()` does not follow the link and returns information about the link itself (its own type, permissions, size and timestamps).

In `ls`, `lstat()` is the right choice because `ls -l` has to show a symbolic link as a link: the type character is `l`, the size and date are those of the link, and the target is printed as `name -> target`. If I used `stat()`, a symlink would look like an ordinary file or directory, and a broken (dangling) link would make `stat()` fail, so the entry could not be displayed correctly. `lstat()` is also what lets the recursive `-R` option avoid following symlinks to directories.

### Q2. How can bitwise operators and macros like `S_IFDIR` or `S_IRUSR` extract the file type and permissions from `st_mode`?

`st_mode` is a single integer. The upper bits hold the file type and the lower bits hold the permission bits (plus the setuid, setgid and sticky bits).

**File type:** the type is stored in a group of bits, so I first mask them with `S_IFMT` and then compare for equality, for example `(st_mode & S_IFMT) == S_IFDIR`. The macros `S_ISDIR(m)`, `S_ISREG(m)`, `S_ISLNK(m)` and so on do exactly this. A plain `st_mode & S_IFDIR` would be wrong, because the type values share bits (a block device or a symlink also has some of the same bits set as a directory or regular file).

**Permissions:** each permission is its own bit, so I test it with `&`. For example `st_mode & S_IRUSR` is non-zero if the owner can read, `S_IWUSR` is owner write, `S_IXUSR` is owner execute, and the same pattern applies for group (`S_IRGRP`, `S_IWGRP`, `S_IXGRP`) and others (`S_IROTH`, `S_IWOTH`, `S_IXOTH`). In my `mode_to_string()` function I test the nine bits one by one and write `r`, `w`, `x` or `-` into a string such as `drwxr-xr-x`. The special bits `S_ISUID`, `S_ISGID` and `S_ISVTX` change the execute position to `s`/`S` or `t`/`T`.

```c
if (st.st_mode & S_IRUSR)  /* owner can read */
    str[1] = 'r';
```

---

## Feature 3 (v1.2.0): Column Display (Down Then Across)

### Q1. What is the logic for printing in a "down then across" format? Why is a single loop through the filenames not enough?

First I read all the filenames into an array and find the longest one. Then:

1. `col_width = longest_name + 2` (two spaces between columns).
2. `cols = terminal_width / col_width` (how many columns fit; at least 1).
3. `rows = ceil(count / cols)`, calculated as `(count + cols - 1) / cols`.

Then I print row by row. The item shown in row `r` and column `c` is `names[r + c * rows]`, so consecutive filenames run down a column, and the next column starts after `rows` items. If the index is past the end of the array, that cell is left empty (this happens in the last, shorter column).

A single loop over the filenames is not enough because a terminal prints one line at a time from left to right, but in this layout the names that sit next to each other on one line are `rows` positions apart in the array. A single loop that prints `names[0]`, `names[1]`, `names[2]`, ... would fill each line left to right (the "across" order) instead. Also, the number of rows depends on the total count and the terminal width, so it has to be calculated before anything is printed, which needs an outer loop over rows and an inner loop over columns.

### Q2. What is the purpose of `ioctl`? What would the limitations be of using only a fixed 80-column width?

`ioctl` is a general system call for talking to a device driver. I use it with the request `TIOCGWINSZ` on standard output, which fills a `struct winsize` with the terminal's current size. `ws_col` gives the width in characters, so the program can work out how many columns really fit.

If I used a fixed width of 80, the output would not match the real terminal. On a wide terminal, much of the screen would be wasted and fewer columns would be shown than could fit. On a terminal narrower than 80 columns, lines would be longer than the window, so they would wrap and the columns would look broken. The program also could not adapt to the size of the window at all. In my code, 80 is only a fallback, used when `ioctl` fails (for example when the output is redirected to a file).

---

## Feature 4 (v1.3.0): Horizontal Column Display (`-x`)

### Q1. Compare the implementation complexity of the vertical ("down then across") and horizontal ("across") printing. Which needs more pre-calculation and why?

The vertical layout is more complex and needs more pre-calculation. Before printing the first line, I must know the total number of files, the column width, the number of columns that fit, and the number of rows, because the position of every file depends on the row count (`names[r + c * rows]`). It also needs a nested loop and a check for the incomplete last column.

The horizontal layout is much simpler. It needs only the column width and a running position on the current line. I print the names in their normal order, and when the next name would go past the terminal width, I print a newline and reset the position to 0. It never needs the row count or the index formula, so it can be done in a single loop.

### Q2. What strategy did you use to manage the different display modes (`-l`, `-x`, default)? How does the program decide which function to print with?

I defined an `enum` called `display_mode_t` with three values: `MODE_DEFAULT`, `MODE_LONG` and `MODE_HORIZONTAL`. In `main()`, a `getopt()` loop sets one `mode` variable: `-l` sets `MODE_LONG` and `-x` sets `MODE_HORIZONTAL`. If the user gives both, the last one wins.

After the filenames are read and sorted, `do_ls()` looks at the mode. If it is `MODE_LONG` it calls `display_long()`, if it is `MODE_HORIZONTAL` it calls `display_horizontal()`, and otherwise it calls `display_columns()` (the default down-then-across layout). Using one mode variable keeps the decision in a single place instead of several separate flags.

---

## Feature 5 (v1.4.0): Alphabetical Sort

### Q1. Why is it necessary to read all directory entries into memory before sorting? What are the drawbacks for directories with millions of files?

`readdir()` returns the entries one at a time in an arbitrary order that depends on the file system. To sort, every entry has to be compared with the others, and the name that should come first could be the very last one returned. So nothing can be printed in the correct order until all entries have been read and stored, which is why I keep them in a dynamic array (`realloc` and `strdup`) and sort the whole array with `qsort()`.

For a directory with millions of files this has drawbacks:
- **Memory:** every name (plus its pointer) must be held at once, so memory use grows with the number of files and could become very large or run out.
- **Delay:** no output appears until the whole directory has been read and sorted, so the user waits.
- **Time:** sorting takes about `n log n` comparisons, and growing the array with `realloc` adds copying.

An unsorted `ls` could print each entry as it is read, using almost no memory (the real `ls` has the `-f` option for this).

### Q2. What are the purpose and signature of the comparison function for `qsort()`? How does it work, and why must it take `const void *` arguments?

`qsort(base, count, element_size, compare)` sorts an array but does not know how to compare its elements, so I supply the comparison function. Its signature is:

```c
int compare_names(const void *a, const void *b);
```

`qsort` calls it again and again with pointers to two elements of the array. It must return a negative number if the first element should come before the second, zero if they are equal, and a positive number if the first should come after. `qsort` uses these results to rearrange the array.

The arguments are `const void *` because `qsort` is a generic function that can sort an array of any type (ints, structs, strings), so it can only pass untyped pointers. The `const` promises that the function will not modify the elements. Inside the function I cast the pointers back to the real type. My array holds `char *` strings, so each argument points to a `char *`, and I write `*(const char **)a` to get the string and then return `strcmp(s1, s2)`.

---

## Feature 6 (v1.5.0): Colorized Output

### Q1. How do ANSI escape codes produce color in a Linux terminal? What is the code for printing green text?

ANSI escape codes are special sequences of characters that the terminal does not display but interprets as instructions. A sequence starts with the escape character (`\033`, ASCII 27), followed by `[`, one or more numbers separated by `;`, and ends with the letter `m`. The numbers choose the style: `0` is normal, `1` is bold, `7` is reverse video, and `30` to `37` choose the text color (31 red, 32 green, 34 blue, 35 magenta). The style stays active for everything printed afterwards until it is changed, which is why the text must be followed by the reset code `\033[0m`.

Green text:

```c
printf("\033[0;32m%s\033[0m", filename);
```

Here `\033[0;32m` switches to green, `%s` prints the name, and `\033[0m` resets the terminal to its default color. Without the reset, everything printed afterwards would also be green.

### Q2. Which bits in `st_mode` must be checked to decide whether a file is executable by the owner, group or others?

There are three execute bits: `S_IXUSR` (owner, octal `0100`), `S_IXGRP` (group, `0010`) and `S_IXOTH` (others, `0001`). I test each with the bitwise AND operator, for example `(st_mode & S_IXUSR) != 0` means the owner can execute the file, and likewise for the group and others.

To color a file green if anyone can execute it, I test all three together: `st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)`. Directories also have the execute bit set (it means "may enter"), so my code checks for a directory with `S_ISDIR()` first, before testing the execute bits, otherwise every directory would be green.

---

## Feature 7 (v1.6.0): Recursive Listing (`-R`)

### Q1. What is a "base case" in a recursive function? What is the base case in your recursive `ls`?

A base case is the condition under which a recursive function stops calling itself and just returns. Without a base case, the function would call itself forever until the stack overflows.

In my recursive `ls`, the base case is a directory that contains no subdirectories. The loop that looks for subdirectories finds none, so no further recursive call is made and the function returns to the level above. Two more things make sure the recursion always ends: hidden entries (including `.` and `..`) are skipped when reading, so the program never recurses into the current or parent directory, and I use `lstat()` so symbolic links to directories are not followed (a link that points back to a parent would otherwise make the tree look infinite).

### Q2. Why is it essential to construct a full path before the recursive call? What would happen if you simply called `do_ls("subdir")` from within `do_ls("parent_dir")`?

System calls like `opendir()` and `lstat()` resolve a relative name starting from the program's current working directory, not from the directory that is being listed, and my program never changes its working directory. So the name `subdir` on its own is not enough to find the folder.

If `do_ls("parent_dir")` called `do_ls("subdir")`, the program would look for `./subdir` in the current directory. That usually does not exist, so `opendir()` would fail with "cannot open directory", and the recursion would never reach the nested folders. Worse, if a different folder called `subdir` happened to exist in the current directory, the program would list the wrong one.

Building the full path `parent_dir/subdir` (with `build_path()`) gives a path that is correct wherever the program is started. Each level adds its own name to the path, so it works at any depth (`a/b/c/d`), and the full path is also what is printed in the header, as the real `ls -R` does.
