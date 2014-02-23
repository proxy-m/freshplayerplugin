/*
 * Copyright © 2013-2014  Rinat Ibragimov
 *
 * This file is part of FreshPlayerPlugin.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pp_resource.h"
#include <glib.h>
#include <stdlib.h>

#define FREE_IF_NOT_NULL(ptr)   if (ptr) { free(ptr); ptr = NULL; }

static GArray          *res_tbl;
static pthread_mutex_t  res_tbl_lock = PTHREAD_MUTEX_INITIALIZER;

static
__attribute__((constructor))
void
pp_resource_constructor(void)
{
    void *null_val = NULL;
    res_tbl = g_array_new(FALSE, FALSE, sizeof(void*));
    g_array_append_val(res_tbl, null_val);
}

PP_Resource
pp_resource_allocate(enum pp_resource_type_e type)
{
    void *ptr;

#define ALLOC_HELPER(typename)              \
    ptr = calloc(sizeof(typename), 1);      \
    ((typename *)ptr)->_.type = type; \
    ((typename *)ptr)->_.ref_cnt = 1;

    pthread_mutex_lock(&res_tbl_lock);
    switch (type) {
    case PP_RESOURCE_URL_LOADER:
        ALLOC_HELPER(struct pp_url_loader_s);
        break;
    case PP_RESOURCE_URL_REQUEST_INFO:
        ALLOC_HELPER(struct pp_url_request_info_s);
        break;
    case PP_RESOURCE_URL_RESPONSE_INFO:
        ALLOC_HELPER(struct pp_url_response_info_s);
        break;
    case PP_RESOURCE_VIEW:
        ALLOC_HELPER(struct pp_view_s);
        break;
    case PP_RESOURCE_GRAPHICS3D:
        ALLOC_HELPER(struct pp_graphics3d_s);
        break;
    case PP_RESOURCE_IMAGE_DATA:
        ALLOC_HELPER(struct pp_image_data_s);
        break;
    case PP_RESOURCE_GRAPHICS2D:
        ALLOC_HELPER(struct pp_graphics2d_s);
        break;
    case PP_RESOURCE_NETWORK_MONITOR:
        ALLOC_HELPER(struct pp_network_monitor_s);
        break;
    default:
        // fall through
    case PP_RESOURCE_UNKNOWN:
        ptr = calloc(sizeof(struct pp_resource_generic_s), 1);
        ((struct pp_resource_generic_s *)ptr)->type = type;
        break;
    }

    int handle = res_tbl->len;
    g_array_append_val(res_tbl, ptr);

    pthread_mutex_unlock(&res_tbl_lock);
    return handle;
}

void
pp_resource_expunge(PP_Resource resource)
{
    pthread_mutex_lock(&res_tbl_lock);
    if (resource < 1 || resource >= res_tbl->len) {
        pthread_mutex_unlock(&res_tbl_lock);
        return;
    }

    void **ptr = &g_array_index(res_tbl, void *, resource);
    free(*ptr);
    *ptr = NULL;
    pthread_mutex_unlock(&res_tbl_lock);
}

void *
pp_resource_acquire_any(PP_Resource resource)
{
    pthread_mutex_lock(&res_tbl_lock);
    if (resource < 1 || resource >= res_tbl->len) {
        pthread_mutex_unlock(&res_tbl_lock);
        return NULL;
    }

    // TODO: add mutexes for every resource
    // TODO: lock mutex of particular resource handle
    struct pp_resource_generic_s *ptr = g_array_index(res_tbl, void*, resource);

    pthread_mutex_unlock(&res_tbl_lock);
    return ptr;
}

void *
pp_resource_acquire(PP_Resource resource, enum pp_resource_type_e type)
{
    struct pp_resource_generic_s *gr = pp_resource_acquire_any(resource);
    if (gr->type != type) {
        pp_resource_release(resource);
        return NULL;
    }
    return gr;
}

void
pp_resource_release(PP_Resource resource)
{
    // TODO: unlock mutex of particular resource handle
}

enum pp_resource_type_e
pp_resource_get_type(PP_Resource resource)
{
    pthread_mutex_lock(&res_tbl_lock);
    if (resource < 1 || resource >= res_tbl->len) {
        pthread_mutex_unlock(&res_tbl_lock);
        return PP_RESOURCE_UNKNOWN;
    }

    struct pp_resource_generic_s *ptr = g_array_index(res_tbl, void*, resource);
    enum pp_resource_type_e type = ptr->type;

    pthread_mutex_unlock(&res_tbl_lock);
    return type;
}

void
pp_resource_ref(PP_Resource resource)
{
    pthread_mutex_lock(&res_tbl_lock);
    if (resource < 1 || resource >= res_tbl->len) {
        pthread_mutex_unlock(&res_tbl_lock);
        return;
    }
    struct pp_resource_generic_s *ptr = g_array_index(res_tbl, void*, resource);
    ptr->ref_cnt ++;
    pthread_mutex_unlock(&res_tbl_lock);
}

void
pp_resource_unref(PP_Resource resource)
{
    PP_Resource parent = 0;
    pthread_mutex_lock(&res_tbl_lock);
    if (resource < 1 || resource >= res_tbl->len) {
        pthread_mutex_unlock(&res_tbl_lock);
        return;
    }

    struct pp_resource_generic_s *ptr = g_array_index(res_tbl, void*, resource);
    ptr->ref_cnt --;

    switch (ptr->type) {
    case PP_RESOURCE_URL_LOADER:
        if (ptr->ref_cnt <= 0) {
            struct pp_url_loader_s *ul = (void *)ptr;
            FREE_IF_NOT_NULL(ul->headers);
            FREE_IF_NOT_NULL(ul->body);
            FREE_IF_NOT_NULL(ul->url);
        }
        break;
    case PP_RESOURCE_URL_RESPONSE_INFO:
        parent = ((struct pp_url_response_info_s *)ptr)->url_loader;
        break;
    case PP_RESOURCE_IMAGE_DATA:
        if (ptr->ref_cnt <= 0) {
            struct pp_image_data_s *id = (void *)ptr;
            if (id->data)
                free(id->data);
        };
        break;
    case PP_RESOURCE_GRAPHICS2D:
        if (ptr->ref_cnt <= 0) {
            struct pp_graphics2d_s *g2d = (void *)ptr;
            if (g2d->data)
                free(g2d->data);
        }
        break;
    default:
        break;
    }

    pthread_mutex_unlock(&res_tbl_lock);

    if (0 == ptr->ref_cnt) {
        pp_resource_expunge(resource);
        if (parent)
            pp_resource_unref(parent);
    }
}
