# File List

This is a small C99+ library for creating hierarchically sorted file lists.
It searches a directory tree for entries that match specified file types and regular expressions.

## Features
- Different sorting methods to choose from, including natural sort order and locale-aware sorting.
- Can traverse the directory tree recursively up to a specified depth or indefinitely.
  - It uses the file system's file type information (if available) to increase performance.
  - Optionally follows symbolic links.
  - Checks for both symlink and hard link file system loops.

## How a file list looks like

```
./foo.bin            // File names first...
./readme.doc
./README.md
./C/example.c        // ...then directories, and so on.
./C/example.h
./C/include/foobar.h
...
```

# Documentation

The purpose of this library is to make working with files a little easier. The created file lists are hierarchically sorted, ready to be processed in "proper" order. A file list in this context is a dynamically allocated array of strings (char **) that ends with a terminating NULL pointer.

## Functions

### file_list_create()

```C
ssize_t file_list_create(char ***file_list, int file_type, const char *regex,
    const char *dir, int depth, int flags, enum FL_SORT_METHOD);
```

Creates a sorted list of files that are found inside a specified
directory.

#### Return value

On success, the number of found files is returned.
On error, -1 is returned and errno is set to indicate the error. The errno
value E2BIG means the file list's size has reached FL_MAX_LIST_SIZE, and at
least 1 file could not be added and is missing.
On all other errors, the file list is deallocated and set to NULL.

#### Parameters

Parameter        | Description
-----------------|--------------------------------------------------------------
`file_list`      | A pointer used to allocate and save the file list.
`file_type`      | Values that can be combined via bitwise OR to filter files by file type.
`regex`          | By default, a case-insensitive POSIX Extended [Regular Expression](https://en.wikibooks.org/wiki/Regular_Expressions/POSIX_Basic_Regular_Expressions) used to filter files by name.
`dir`            | The directory in which the file search starts.
`depth`          | The maximum level of directory recursion; 0 means "no recursion" and -1 means "unlimited recursion".
`flags`          | Various flags that can be combined via bitwise OR.
`FL_SORT_METHOD` | Specifies the method by which the file list is sorted.

##### Values for parameter `file_type`

These values can be combined via the bitwise OR operator (e.g. `FL_DIR | FL_REG`).

Value        | Meaning
-------------|------------------------------------------------------------------
`FL_BLK`     | Block device
`FL_CHR`     | Character device
`FL_DIR`     | Directory
`FL_FIFO`    | Named pipe (FIFO)
`FL_LNK`     | Symbolic link
`FL_REG`     | Regular file
`FL_SOCK`    | UNIX domain socket
`FL_UNKNOWN` | Unknown file type
Just `0`     | All types

##### Values for parameter `flags`

These values can be combined via the bitwise OR operator.

Value             | Meaning
------------------|-------------------------------------------------------------
`FL_FOLLOW_LINKS` | Follow symbolic links.
`FL_DIR_SEP`      | Append a directory separator to directory file list items.
`FL_REGEX_CASE`   | Enable case-sensitive regex matching.
`FL_REGEX_BASIC`  | Enable basic regular expressions (disabling extended RE).
`FL_XDEV`         | Do not descend into directories that lead to other file systems.

##### Values for parameter `FL_SORT_METHOD`

Value             | Meaning
------------------|-------------------------------------------------------------
`FL_SORT_NONE`    | Do not sort the file list.
`FL_SORT_DEFAULT` | Sort by raw bytes, semi-case-insensitively (lowercase first, smaller strings first).
`FL_SORT_NATURAL` | Same as `FL_SORT_DEFAULT`, but additionally sort numbers in natural sort order.
`FL_SORT_COLLATE` | Sort with `strcoll()` to take into account the current C locale's `LC_COLLATE` setting. May improve sorting for other languages but can be comparably slow.
`FL_SORT_ASCII`   | Sort with `strcmp()`, which means ASCIIbetical order and is the fastest sorting method.

For `FL_SORT_COLLATE` to have an effect, it is necessary to change the C locale with `setlocale(LC_ALL, "");` or at least `setlocale(LC_COLLATE, "");`.

### file_list_destroy()

```C
void file_list_destroy(char ***file_list);
```

Frees memory space previously allocated by create_file_list().

### file_list_merge()

```C
ssize_t file_list_merge(char ***destination, size_t n_dest,
    const char ***source, size_t n_source, enum FL_SORT_METHOD);
```

Merges two file lists by appending a copy of <source> to <destination> and optionally sorting it.
Specifying the lists' sizes is faster but optional (0 meaning unspecified).
On error, -1 is returned, errno is set to indicate the error, and the destination list remains unchanged.

## Preprocessor directives

```C
// Enables debug output.
#define FL_DEBUG
```

```C
// For implementations whose dirent structure does not have the member .d_type
// (which is not mandated by POSIX), FL_NO_DTYPE must be defined to be able to
// compile.
#define FL_NO_D_TYPE
```

## Example code

```C
#include "file_list.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [DIRECTORY]\n", argv[0]);
        return 1;
    }

    char *dir;
    if (argc == 1)
        dir = ".";
    else
        dir = argv[1];

    // 1. Create a file list.
    char **file_list;
    ssize_t n = file_list_create(&file_list, FL_REG, NULL, dir, -1, 0,
        FL_SORT_DEFAULT);
    if (n == -1)
    {
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return 1;
    }

    // 2. Do something with the file list.
    for (ssize_t i = 0; i < n; i++)
        printf("%s\n", file_list[i]);

    // 3. Free memory space.
    file_list_destroy(&file_list);
}
```
