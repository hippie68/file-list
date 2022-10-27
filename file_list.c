// A C99+ library for creating hierarchically sorted file lists.
// Copyright (c) 2022 hippie68 (https://github.com/hippie68/file-list)

#include "file_list.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#ifdef FL_NO_DTYPE
#include <limits.h>
#endif
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DIR_SEPARATOR '/'

#ifdef FL_DEBUG
#define DEBUG_PRINTF(...) fprintf(stderr, "FILE_LIST: " __VA_ARGS__)
#else
#define DEBUG_PRINTF(...)
#endif

// A file list's initial and maximum array sizes. The initial size will
// dynamically grow until it hits the maximum size. Can be changed arbitrarily.
#define FL_INITIAL_LIST_SIZE 512
#define FL_MAX_LIST_SIZE 1048576

#if FL_MAX_LIST_SIZE > SIZE_MAX - 1
#define FL_MAX_LIST_SIZE (SIZE_MAX - 1)
#endif

// String comparisons ----------------------------------------------------------

// Compares strings using alphabetical order.
static int strcmp_default(const char *s1, const char *s2)
{
    register unsigned char *c1 = (unsigned char *) s1;
    register unsigned char *c2 = (unsigned char *) s2;

    int temp_ret = 0;

    do
    {
        // Compare single characters that don't match.
        if (*c1 != *c2)
        {
            int lc1 = tolower(*c1);
            int lc2 = tolower(*c2);

            if (lc1 == lc2) // Case mismatch.
            {
                if (!temp_ret)
                    temp_ret = *c2 - *c1; // Reversed case order (a < A).
            }
            else
                return lc1 - lc2;
        }

        c1++;
        c2++;
    }
    while (*c1 && *c2);

    if (*c1 == *c2) // The strings are of equal length.
        return temp_ret;
    else
        return *c1 - *c2;
}

// Compares strings using natural sort order and alphabetical order.
static int strcmp_natural(const char *s1, const char *s2)
{
    register unsigned char *c1 = (unsigned char *) s1;
    register unsigned char *c2 = (unsigned char *) s2;

    int temp_ret = 0;

    do
    {
        // Compare two substrings of unlimited digit characters as two numbers.
        if (isdigit(*c1) && isdigit(*c2))
        {

            register unsigned char *p1 = c1;
            register unsigned char *p2 = c2;
            unsigned char *z1 = c1;
            unsigned char *z2 = c2;

            // Advance character pointers to the end of the digits.
            while (isdigit(*(c1 + 1)))
                c1++;
            while (isdigit(*(c2 + 1)))
                c2++;

            // Ignore leading zeroes.
            while (*p1 == '0' && p1 < c1)
                p1++;
            while (*p2 == '0' && p2 < c2)
                p2++;

            // Current pointer state:
            // 00000000123456789
            // z       p       c

            int len1 = c1 - p1;
            int len2 = c2 - p2;
            if (len1 != len2)
                return len1 - len2;
            else // The digit substrings are of equal length.
            {
                for (; p1 <= c1 && p2 <= c2; p1++, p2++)
                {
                    if (*p1 != *p2)
                        return *p1 - *p2;
                }

                // The numbers are equal, too, so compare the total length.
                len1 = c1 - z1;
                len2 = c2 - z2;
                if (len1 != len2)
                    return len2 - len1; // "More zeroes" goes first.
            }
        }
        // Compare single non-digit characters that don't match.
        else if (*c1 != *c2)
        {
            int lc1 = tolower(*c1);
            int lc2 = tolower(*c2);

            if (lc1 == lc2) // Case mismatch.
            {
                if (!temp_ret)
                    temp_ret = *c2 - *c1; // Reversed case order (a < A).
            }
            else
                return lc1 - lc2;
        }

        c1++;
        c2++;
    }
    while (*c1 && *c2);

    if (*c1 == *c2) // The strings are of equal length.
        return temp_ret;
    else
        return *c1 - *c2;
}

// Callbacks and related functions for qsort() ---------------------------------

static int qsort_compar(const void *p1, const void *p2,
    int (*compar_fn)(const char *, const char *))
{
    // Separate path and basename parts.
    char *path1 = *(char **) p1;
    char *sep1 = strrchr(path1, DIR_SEPARATOR);
    *sep1 = '\0';
    char *path2 = *(char **) p2;
    char *sep2 = strrchr(path2, DIR_SEPARATOR);
    *sep2 = '\0';

    // Compare paths first...
    int result = compar_fn(path1, path2);
    *sep1 = DIR_SEPARATOR;
    *sep2 = DIR_SEPARATOR;

    // ...and basenames only if necessary.
    if (result == 0)
        result = compar_fn(sep1 + 1, sep2 + 1);

    return result;
}

static int qsort_compar_default(const void *p1, const void *p2)
{
    return qsort_compar(p1, p2, strcmp_default);
}

static int qsort_compar_natural(const void *p1, const void *p2)
{
    return qsort_compar(p1, p2, strcmp_natural);
}

static int qsort_compar_collate(const void *p1, const void *p2)
{
    return qsort_compar(p1, p2, strcoll);
}

static int qsort_compar_ascii(const void *p1, const void *p2)
{
    return qsort_compar(p1, p2, strcmp);
}

// Helper function for file_list_create() and file_list_merge().
static int (*get_compar_fn(int sort_method))(const void *, const void *)
{
    switch (sort_method)
    {
        case FL_SORT_DEFAULT:
            return qsort_compar_default;
        case FL_SORT_NATURAL:
            return qsort_compar_natural;
        case FL_SORT_COLLATE:
            return qsort_compar_collate;
        case FL_SORT_ASCII:
            return qsort_compar_ascii;
        default:
            return NULL;
    }
}

// Stat stack ------------------------------------------------------------------

#define STAT_STACK_INITIAL_SIZE 512

struct stat_stack
{
    ssize_t top;         // Keeps track of the topmost array element.
    size_t size;         // The array's maximum size.
    struct stat **array;
};

// Creates a new struct stat_stack.
// Returns -1 on error, otherwise 0.
static int stat_stack_create(struct stat_stack *stack)
{
    stack->array = malloc(STAT_STACK_INITIAL_SIZE * sizeof(struct stat *));
    if (stack->array == NULL)
        return -1;

    stack->top = -1;
    stack->size = STAT_STACK_INITIAL_SIZE;

    return 0;
}

// Adds a new stat buffer to the stack.
// The values are supposed to point to existing variables.
static int stat_stack_push(struct stat_stack *stack, struct stat *sb)
{
    if ((size_t) (stack->top + 1) == stack->size)
    {
        size_t new_size = stack->size * 2;
        void *p = realloc(stack->array, new_size * sizeof(struct stat *));
        if (p == NULL)
            return -1;
        stack->array = p;
        stack->size = new_size;
    }

    stack->array[++stack->top] = sb;
    return 0;
}

// Removes a stat entry from the stack.
static void stat_stack_pop(struct stat_stack *stack)
{
    stack->top--;
}

// Deallocates a stat stack's memory-allocated parts.
static void stat_stack_destroy(struct stat_stack *stack)
{
    free(stack->array);
}

// Checks if a stat stack contains a specific inode and device combination.
static int is_directory_loop(struct stat_stack *stack, struct stat *sb)
{
    for (size_t i = 0; i <= (size_t) stack->top; i++)
    {
        if (stack->array[i]->st_ino == sb->st_ino
            && stack->array[i]->st_dev == sb->st_dev)
        {
            return 1;
        }
    }

    return 0;
}

// -----------------------------------------------------------------------------

// Creates a new string by concatenating dir and file (which must not be NULL),
// inserting a directory separator character if necessary.
static char *create_path(const char *dir, const char *file)
{
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);
    char *path;

    if (dir[dir_len - 1] == DIR_SEPARATOR)
    {
        path = malloc(dir_len + file_len + 1);
        if (path == NULL)
            return NULL;
        memcpy(path, dir, dir_len);
        memcpy(path + dir_len, file, file_len + 1);
    }
    else
    {
        path = malloc(dir_len + file_len + 2);
        if (path == NULL)
            return NULL;
        memcpy(path, dir, dir_len);
        path[dir_len] = DIR_SEPARATOR;
        memcpy(path + dir_len + 1, file, file_len + 1);
    }

    return path;
}

// Removes all superflous and trailing directory separators from a directory
// path, returning a dynamically allocated string.
static char *create_clean_dir(const char *directory)
{
    if (directory == NULL)
        return NULL;

    size_t len = strlen(directory);
    if (len == 0)
        return NULL;

    char *clean_dir = malloc(len + 1);
    if (clean_dir == NULL)
        return NULL;

    // Build new string, omitting superflous directory separators.
    size_t pos = 0;
    int prev_char_is_separator = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (directory[i] == DIR_SEPARATOR)
        {
            if (prev_char_is_separator)
                continue;
            else
                prev_char_is_separator = 1;
        }
        else
            prev_char_is_separator = 0;

        clean_dir[pos++] = directory[i];
    }
    clean_dir[pos] = '\0';

    // Remove trailing directory separators.
    while (clean_dir[--pos] == DIR_SEPARATOR && pos != 0)
        clean_dir[pos] = '\0';

    return clean_dir;
}

// Adds a path to an intermediate file list array.
// size: the currently saved number of array elements
// max_size: the array's currently allocated memory size
// On error, 1 is returned and errno is set.
static inline int file_list_add(char ***file_list, size_t *size,
    size_t *max_size, char *path)
{
    // Resize file list if needed.
    if (*size == *max_size)
    {
        // Abort if the list has already reached the maximum allowed size.
        if (*size == FL_MAX_LIST_SIZE)
        {
            errno = E2BIG;
            return -1;
        }

        size_t new_size = *max_size * 2;

        // Overflow check.
        if (new_size < *size)
            new_size = SIZE_MAX;

        if (new_size > FL_MAX_LIST_SIZE)
            *max_size = FL_MAX_LIST_SIZE;
        else
            *max_size = new_size;

        DEBUG_PRINTF("Resizing file list array: max. %zu elements\n",
            *max_size);
        char **p = realloc(*file_list, *max_size * sizeof(char *));
        if (p == NULL)
            return -1;
        *file_list = p;
    }

    // Add file name to file list.
    (*file_list)[(*size)++] = path;

    return 0;
}

// Returns 1 if a file name matches a compiled regular expression, otherwise 0.
static int matches_regex(const char *file_name, const regex_t *regex)
{
    int ret = regexec(regex, file_name, 0, NULL, 0);
    if (ret == 0)
        return 1;

#ifdef FILE_LIST_DEBUG
    if (ret != REG_NOMATCH)
    {
        char buf[512];
        regerror(ret, regex, buf, sizeof(buf));
        DEBUG_PRINTF("regexec(): %d (%s): \"%s\"\n", ret, buf, file_name);
    }
#endif

    return 0;
}

// Recursively traverses a directory to populate a file list.
// On error, -1 is returned and errno is set.
static int parse_file_tree(char ***file_list, size_t *n_file_list,
    size_t *n_file_list_max, int *file_type_arr, regex_t *file_ext,
    char *directory, int directory_depth, struct stat_stack *stack, int flags)
{
// Creates the current file's path string, if not already done.
#define CREATE_CURRENT_PATH()                                  \
    do                                                         \
    {                                                          \
        if (current_path == NULL)                              \
        {                                                      \
            current_path = create_path(directory, dp->d_name); \
            if (current_path == NULL)                          \
            {                                                  \
                closedir(dir);                                 \
                return -1;                                     \
            }                                                  \
        }                                                      \
    }                                                          \
    while (0)

    DIR *dir = opendir(directory);
    if (dir == NULL)
    {
        DEBUG_PRINTF("opendir(): errno %d (%s): \"%s\"\n", errno,
            strerror(errno), directory);
        if (errno != EACCES)
            return -1;
        else
            return 0;
    }

    struct dirent *dp;
    while ((dp = readdir(dir)) != NULL)
    {
        // Ignore current and parent directory.
        if (dp->d_name[0] == '.')
        {
            if (dp->d_name[1] == '\0')
                continue;
            else if (dp->d_name[1] == '.' && dp->d_name[2] == '\0')
                continue;
        }

        struct stat sb;
        char *current_path = NULL;
        unsigned char current_type;

        // Get the file's type.
#ifndef FL_NO_D_TYPE
        // Use stat even if the directory entry's .d_type is available, for:
        // - DT_DIR: to get device information for loop checking.
        // - DT_UNKNOWN: to get the type (for some FS it's always DT_UNKNOWN).
        // - DT_LNK: to get the linked file's type.
        if (dp->d_type == DT_DIR || dp->d_type == DT_UNKNOWN
            || (dp->d_type == DT_LNK && (flags & FL_FOLLOW_LINKS)))
#endif
        {
            CREATE_CURRENT_PATH();
            int ret;
            if (flags & FL_FOLLOW_LINKS)
                ret = stat(current_path, &sb);
            else
                ret = lstat(current_path, &sb);
            if (ret == -1)
            {
                DEBUG_PRINTF("stat(): errno %d (%s): \"%s\"\n", errno,
                    strerror(errno), current_path);
                free(current_path);
                continue;
            }
            current_type = sb.st_mode >> 12 & 017; // Convert to .d_type value.
        }
#ifndef FL_NO_D_TYPE
        else
            current_type = dp->d_type;
#endif

        // Traverse next directory.
        if (directory_depth && current_type == 4) // 4: DT_DIR
        {
            // Ignore directory if following it would cause a loop. Don't add it
            // to the file list.
            if (is_directory_loop(stack, &sb))
            {
                DEBUG_PRINTF("Directory loop detected: \"%s\"\n", current_path);
                free(current_path);
                continue;
            }

            // Ignore directory if it leads to a different device.
            if (flags & FL_XDEV && stack->array[0]->st_dev != sb.st_dev)
            {
                DEBUG_PRINTF("Ignoring other file system: \"%s\"\n",
                    current_path);
            }
            else
            {
                if (stat_stack_push(stack, &sb))
                {
                    free(current_path);
                    closedir(dir);
                    return -1;
                }

                if (parse_file_tree(file_list, n_file_list,
                    n_file_list_max, file_type_arr, file_ext, current_path,
                    directory_depth > 0 ? directory_depth - 1 : directory_depth,
                    stack, flags))
                {
                    free(current_path);
                    closedir(dir);
                    return -1;
                }

                stat_stack_pop(stack);
            }
        }

        // Add file name to file list.
        if (file_type_arr[current_type] == 1)
        {
            // Ignore file if the regular expression doesn't match.
            if (file_ext && !matches_regex(dp->d_name, file_ext))
            {
                if (current_path)
                    free(current_path);
                continue;
            }

            CREATE_CURRENT_PATH();

            // If requested, add a trailing directory separator.
            if (current_type == 4 && flags & FL_DIR_SEP)
            {
                size_t new_len = strlen(current_path) + 1;
                char *new_path = realloc(current_path, new_len + 1);
                if (new_path == NULL)
                {
                    free(current_path);
                    closedir(dir);
                    return -1;
                }

                new_path[new_len - 1] = DIR_SEPARATOR;
                new_path[new_len] = '\0';
                current_path = new_path;
            }

            if (file_list_add(file_list, n_file_list, n_file_list_max,
                current_path) == -1)
            {
                free(current_path);
                closedir(dir);
                return -1;
            }
        }
        else if (current_path)
            free(current_path);
    }

    closedir(dir);
    return 0;

#undef CREATE_CURRENT_PATH
}

// Public functions ------------------------------------------------------------

ssize_t file_list_create(char ***file_list, int file_type,
    const char *regex_pattern, const char *dir, int depth, int flags,
    enum FL_SORT_METHOD sort_method)
{
    // Allocate initial memory for file list.
    *file_list = malloc(FL_INITIAL_LIST_SIZE * sizeof(char *));
    if (*file_list == NULL)
        return -1;
    size_t file_list_size = 0;
    size_t file_list_size_max = FL_INITIAL_LIST_SIZE;

    // Create file type lookup array (its indexes are DT_ values from dirent.h).
    int file_type_arr[13] = { 0 };
    if (file_type == 0)
    {
        for (int i = 0; i < 13; i++)
            file_type_arr[i] = 1;
    }
    else
    {
        if (file_type & FL_UNKNOWN)
            file_type_arr[0] = 1;
        if (file_type & FL_FIFO)
            file_type_arr[1] = 1;
        if (file_type & FL_CHR)
            file_type_arr[2] = 1;
        if (file_type & FL_DIR)
            file_type_arr[4] = 1;
        if (file_type & FL_BLK)
            file_type_arr[6] = 1;
        if (file_type & FL_REG)
            file_type_arr[8] = 1;
        if (file_type & FL_LNK)
            file_type_arr[10] = 1;
        if (file_type & FL_SOCK)
            file_type_arr[12] = 1;
    }

    // Compile regular expression.
    regex_t regex;
    if (regex_pattern)
    {
        int regex_flags = REG_NOSUB;
        if (flags)
        {
            if (!(flags & FL_REGEX_BASIC))
                regex_flags |= REG_EXTENDED;
            if (!(flags & FL_REGEX_CASE))
                regex_flags |= REG_ICASE;
        }

        if (regcomp(&regex, regex_pattern, regex_flags))
        {
            free(*file_list);
            return -1;
        }
    }

    // Strip superfluous directory separators.
    char *start_dir = create_clean_dir(dir);
    if (start_dir == NULL)
    {
        free(*file_list);
        if (regex_pattern)
            regfree(&regex);
        return -1;
    }

    // Set up initial stat stack, which is used for loop detection.
    struct stat_stack stack;
    if (stat_stack_create(&stack))
    {
        free(*file_list);
        if(regex_pattern)
            regfree(&regex);
        free(start_dir);
        return -1;
    }
    struct stat sb;
    if (stat(dir, &sb))
    {
        free(*file_list);
        if (regex_pattern)
            regfree(&regex);
        free(start_dir);
        stat_stack_destroy(&stack);
        return -1;
    }
    stat_stack_push(&stack, &sb);

    // Populate file list.
    int ret = parse_file_tree(file_list, &file_list_size, &file_list_size_max,
        file_type_arr, regex_pattern ? &regex : NULL, start_dir, depth, &stack,
        flags);
    if (regex_pattern)
        regfree(&regex);
    free(start_dir);
    stat_stack_destroy(&stack);
    if (ret && errno != E2BIG)
    {
        for (size_t i = 0; i < file_list_size; i++)
            free((*file_list)[i]);
        free(*file_list);
        *file_list = NULL;
        return -1;
    }

    // Trim file list and make it NULL-terminated.
    char **p = realloc(*file_list, (file_list_size + 1) * sizeof(char *));
    if (p == NULL)
        return -1;
    *file_list = p;
    (*file_list)[file_list_size] = NULL;

    // Sort file list.
    int (*compar_fn)(const void *, const void *) = get_compar_fn(sort_method);
    if (compar_fn)
        qsort(*file_list, file_list_size, sizeof(char *), compar_fn);

    return file_list_size;
}

// Frees memory space previously allocated by file_list_create().
void file_list_destroy(char ***file_list)
{
    if (*file_list == NULL)
        return;

    for (size_t i = 0; (*file_list)[i] != NULL; i++)
        free((*file_list)[i]);
    free(*file_list);
    *file_list = NULL;
}

// Helper function for file_list_merge(); returns a file list's size.
static size_t file_list_getsize(const char **file_list)
{
    size_t n = 0;
    while (*file_list++ != NULL)
        n++;

    return n;
}

// Merges two file lists by appending a copy of <source> to <destination> and
// optionally sorting it.
// Specifying the lists' sizes is faster but optional (0 meaning unspecified).
// On error, -1 is returned, errno is set to indicate the error, and the
// destination list remains unchanged.
ssize_t file_list_merge(char ***destination, size_t n_dest,
    const char ***source, size_t n_source, enum FL_SORT_METHOD sort_method)
{
    if (n_dest == 0)
        n_dest = file_list_getsize((const char **) *destination);
    if (n_source == 0)
        n_source = file_list_getsize(*source);

    size_t n = n_dest + n_source;
    if (n < n_source || n + 1 == 0) // Overflow check.
    {
        errno = ERANGE;
        return -1;
    }

    char **p = realloc(*destination, (n + 1) * sizeof(char *));
    if (p == NULL)
        return -1;
    *destination = p;
    memcpy(*destination + n_dest, *source, n_source * sizeof(char *));
    (*destination)[n] = NULL;
    *source = NULL;

    // Sort concatenated file list.
    int (*compar_fn)(const void *, const void *) = get_compar_fn(sort_method);
    if (compar_fn)
        qsort(*destination, n, sizeof(char *), qsort_compar_default);

    return n;
}
