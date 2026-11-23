// Fuzz Mir's Xcursor theme loader (src/miral/xcursor.c), the code miral::CursorTheme /
// XCursorLoader uses to read a cursor theme from the user's theme directories: the theme's
// index.theme (its "Inherits=" list, followed recursively) and every cursor file in each theme's
// cursors/ directory.
//
// Input layout (all parts are untrusted theme content, written as files each iteration):
//   u16le A | u16le B | A bytes  -> <dirA>/fuzz/index.theme
//                     | B bytes  -> <dirA>/fuzz/cursors/left_ptr
//                     | rest     -> <dirB>/base/cursors/left_ptr
// (A and B are clamped to what is left, so every byte string is a valid input.) The search path
// XCURSOR_PATH="<dirA>:<dirB>" is set once before the first load (xcursor.c reads it once, via
// pthread_once), so the loader walks two path elements for every theme and can follow
// "Inherits=base" into the second one. <dirA>/fuzz/cursors also holds an empty sub-directory,
// which the directory scan must skip. Each input rewrites all three files, so every result depends
// on that input only; the directory layout itself is created once and never changes.
#include "xcursor.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char index_file[4096], cursor_file[4096], base_cursor_file[4096];

static void on_images(XcursorImages *images, void *user_data)
{
    uint32_t *sum = (uint32_t *)user_data;
    for (int i = 0; i < images->nimage; i++) {
        XcursorImage const *img = images->images[i];
        // Touch every pixel, as XCursorLoader does when it converts the image.
        size_t n = (size_t)img->width * img->height;
        for (size_t p = 0; p < n; p++)
            *sum += img->pixels[p];
        *sum += img->xhot + img->yhot + img->delay;
    }
    if (images->name)
        *sum += (uint32_t)strlen(images->name);
    XcursorImagesDestroy(images);
}

static void mkdir_or_die(char const *path)
{
    if (mkdir(path, 0700) != 0) { perror(path); abort(); }
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    char const *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    char root[2048], p[4096], path[4200];
    snprintf(root, sizeof root, "%s/mir-xcursor-XXXXXX", tmp);
    if (!mkdtemp(root)) { perror("mkdtemp"); abort(); }

    snprintf(p, sizeof p, "%s/a", root);                 mkdir_or_die(p);
    snprintf(p, sizeof p, "%s/a/fuzz", root);            mkdir_or_die(p);
    snprintf(p, sizeof p, "%s/a/fuzz/cursors", root);    mkdir_or_die(p);
    snprintf(p, sizeof p, "%s/a/fuzz/cursors/subdir", root); mkdir_or_die(p);
    snprintf(p, sizeof p, "%s/b", root);                 mkdir_or_die(p);
    snprintf(p, sizeof p, "%s/b/base", root);            mkdir_or_die(p);
    snprintf(p, sizeof p, "%s/b/base/cursors", root);    mkdir_or_die(p);

    snprintf(index_file, sizeof index_file, "%s/a/fuzz/index.theme", root);
    snprintf(cursor_file, sizeof cursor_file, "%s/a/fuzz/cursors/left_ptr", root);
    snprintf(base_cursor_file, sizeof base_cursor_file, "%s/b/base/cursors/left_ptr", root);
    snprintf(path, sizeof path, "%s/a:%s/b", root, root);
    setenv("XCURSOR_PATH", path, 1);
    return 0;
}

static void write_file(char const *name, uint8_t const *data, size_t size)
{
    FILE *f = fopen(name, "wb");
    if (!f) abort();
    if (size && fwrite(data, 1, size, f) != size) abort();
    fclose(f);
}

int LLVMFuzzerTestOneInput(uint8_t const *data, size_t size)
{
    size_t a = 0, b = 0;
    if (size >= 4) {
        a = data[0] | (size_t)data[1] << 8;
        b = data[2] | (size_t)data[3] << 8;
        data += 4; size -= 4;
    } else {
        data += size; size = 0;
    }
    if (a > size) a = size;
    write_file(index_file, data, a);
    data += a; size -= a;
    if (b > size) b = size;
    write_file(cursor_file, data, b);
    data += b; size -= b;
    write_file(base_cursor_file, data, size);

    uint32_t sum = 0;
    xcursor_load_theme("fuzz", 24, on_images, &sum);
    return 0;
}
