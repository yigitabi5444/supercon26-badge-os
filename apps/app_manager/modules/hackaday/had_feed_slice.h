#pragma once
#include <stddef.h>
#include "hackaday.h"

enum { HAD_POSTS_MAX = 10, HAD_IMAGES_MAX = 64 };
/* Input contains size content bytes plus a trailing NUL.
   Provide HAD_POSTS_MAX and HAD_IMAGES_MAX slots. The caller owns returned
   article text allocations and may free them after the reader is finished.
   Parser runs serially on the module's owning task. */
bool had_feed_parse(const char *input, size_t size, post_t *post_storage,
                    img_t *image_storage, void *(*alloc)(size_t),
                    int *post_count, int *image_count);
