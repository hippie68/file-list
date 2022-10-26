// A C99+ library for creating hierarchically sorted file lists.
// Copyright (c) 2022 hippie68 (https://github.com/hippie68/file-list)

#ifndef FILE_LIST_H
#define FILE_LIST_H

#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>

// File types for file_list_create().
#define FL_UNKNOWN   1
#define FL_FIFO      2
#define FL_CHR       4
#define FL_DIR       8
#define FL_BLK      16
#define FL_REG      32
#define FL_LNK      64
#define FL_SOCK    128

// Flags for file_list_create().
#define FL_FOLLOW_LINKS  1
#define FL_DIR_SEP       2
#define FL_REGEX_CASE    4
#define FL_REGEX_BASIC   8
#define FL_XDEV         16

// Sort methods for file_list_create().
enum FL_SORT_METHOD
{
    FL_SORT_NONE,
    FL_SORT_DEFAULT,
    FL_SORT_NATURAL,
    FL_SORT_COLLATE,
    FL_SORT_ASCII,
};

// Enables debug output.
#define FL_DEBUG

// For implementations whose dirent structure does not have the member .d_type
// (which is not mandated by POSIX), FL_NO_DTYPE must be defined to be able to
// compile.
//#define FL_NO_D_TYPE

// Creates a sorted list of files (char **) that are found inside a specified
// directory. The list is saved in dynamically allocated memory and ends with a
// terminating NULL pointer.
//
// Parameters:
// file_list       A pointer used to allocate and save the file list.
// file_type       Values that can be combined via bitwise OR operator ("|") to
//                 filter files by file type.
// regex           By default, a case-insensitive POSIX Extended Regular
//                 Expression used to match only certain file names.
// dir             The directory in which the file search starts.
// depth           The maximum level of directory recursion; 0 means
//                 "no recursion" and -1 means "unlimited recursion".
// flags           Various flags that can be combined via bitwise OR.
// FL_SORT_METHOD  Specifies the method by which the file list is sorted.
//
// Values for parameter <file_type> (can be combined):
// FL_BLK      Block device
// FL_CHR      Character device
// FL_DIR      Directory
// FL_FIFO     Named pipe (FIFO)
// FL_LNK      Symbolic link
// FL_REG      Regular file
// FL_SOCK     UNIX domain socket
// FL_UNKNOWN  Unknown file type
// Just "0"    All types
//
// Values for parameter <flags> (can be combined):
// FL_FOLLOW_LINKS   Follow symbolic links.
// FL_DIR_SEP        Append a directory separator to directory file list items.
// FL_REGEX_CASE     Enable case-sensitive regex matching.
// FL_REGEX_BASIC    Enable basic regular expressions (disabling extended RE).
// FL_XDEV           Do not descend into directories that lead to other file
//                   systems.
//
// Values for parameter <FL_SORT_METHOD>:
// FL_SORT_NONE     Do not sort the file list.
// FL_SORT_DEFAULT  Sort by raw bytes, semi-case-insensitively (lowercase first,
//                  smaller strings first).
// FL_SORT_NATURAL  Same as FL_SORT_DEFAULT, but additionally sort numbers in
//                  natural sort order.
// FL_SORT_COLLATE  Sort with strcoll() to take into account the current C
//                  locale's LC_COLLATE setting. May improve sorting results for
//                  other languages but can be comparably slow.
// FL_SORT_ASCII    Sort with strcmp(), which means ASCIIbetical order and is
//                  the fastest sorting method.
//
// Return value:
// On success, the number of found files is returned.
// On error, -1 is returned and errno is set to indicate the error. The errno
// value E2BIG means the file list's size has reached FL_MAX_LIST_SIZE, and at
// least 1 file could not be added and is missing.
// On all other errors, the file list is deallocated and set to NULL.
ssize_t file_list_create(char ***file_list, int file_type, const char *regex,
    const char *dir, int depth, int flags, enum FL_SORT_METHOD);

// Frees memory space previously allocated by create_file_list().
void file_list_destroy(char ***file_list);

// Merges two file lists by appending a copy of <source> to <destination> and
// optionally sorting it.
// Specifying the lists' sizes is faster but optional (0 meaning unspecified).
// On error, -1 is returned, errno is set to indicate the error, and the
// destination list remains unchanged.
ssize_t file_list_merge(char ***destination, size_t n_dest,
    const char ***source, size_t n_source, enum FL_SORT_METHOD);

#endif
