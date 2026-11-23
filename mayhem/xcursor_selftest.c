// Known-answer check for src/miral/xcursor.c (used by mayhem/test.sh).
// Loads the theme "default" from XCURSOR_PATH (the upstream testing theme in
// tests/mir_test_framework/testing-cursor-theme) and prints one line per cursor:
//   <name> <nimage> <size> <width>x<height> hot=<x>,<y> px0=<hex> center=<hex>
#include "xcursor.h"
#include <stdio.h>
#include <stdlib.h>

static void on_images(XcursorImages *images, void *user_data)
{
    (void)user_data;
    for (int i = 0; i < images->nimage; i++) {
        XcursorImage const *img = images->images[i];
        unsigned c = img->width / 2 + (img->height / 2) * img->width;
        printf("%s %d %u %ux%u hot=%u,%u px0=%08x center=%08x\n",
               images->name ? images->name : "(null)", images->nimage, img->size,
               img->width, img->height, img->xhot, img->yhot,
               img->pixels[0], img->pixels[c]);
    }
    XcursorImagesDestroy(images);
}

int main(int argc, char **argv)
{
    int size = argc > 1 ? atoi(argv[1]) : 24;
    xcursor_load_theme("default", size, on_images, NULL);
    return 0;
}
