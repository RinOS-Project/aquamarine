/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aq_text.h"

static void* test_alloc(uint32_t size) { return malloc((size_t)size); }

static void test_free(void* pointer) { free(pointer); }

/* Keep this test independent of the surface allocator implementation. */
AqAllocFn g_aq_alloc;
AqFreeFn g_aq_free;

typedef struct PathSource {
    const uint8_t* bytes;
    uint64_t size;
    uint32_t calls;
} PathSource;

static RinResourceCatalogStatus read_path(
    void* context, const char* path, uint32_t path_size, uint8_t* output,
    uint64_t output_capacity, uint64_t* output_size) {
    PathSource* source = (PathSource*)context;
    assert(source != NULL && path != NULL && output_size != NULL);
    assert(path_size == 15u && memcmp(path, "/fonts/path.psf", path_size) == 0);
    ++source->calls;
    if (source->size > output_capacity) return RIN_RESOURCE_CATALOG_BUFFER_TOO_SMALL;
    memcpy(output, source->bytes, (size_t)source->size);
    *output_size = source->size;
    return RIN_RESOURCE_CATALOG_OK;
}

int main(void) {
    uint8_t psf1[4u + 256u];
    uint8_t storage[sizeof(psf1)];
    uint8_t too_small[sizeof(psf1) - 1u];
    RinResourceCatalogEntryV1 entries[2];
    RinResourceCatalogV1 catalog;
    PathSource source;
    uint64_t storage_size = UINT64_MAX;
    AqFont* font;

    memset(psf1, 0, sizeof(psf1));
    psf1[0] = 0x36u;
    psf1[1] = 0x04u;
    psf1[2] = 0u;
    psf1[3] = 1u;
    psf1[4u + 65u] = 0x80u;

    memset(entries, 0, sizeof(entries));
    entries[0].struct_size = sizeof(entries[0]);
    entries[0].version = RIN_RESOURCE_CATALOG_VERSION_1;
    entries[0].type = RIN_RESOURCE_CATALOG_TYPE_FONT;
    entries[0].resource_id = 7u;
    entries[0].flags = RIN_RESOURCE_CATALOG_SOURCE_BLOB |
                       RIN_RESOURCE_CATALOG_FLAG_IMMUTABLE;
    entries[0].data = psf1;
    entries[0].data_size = sizeof(psf1);
    entries[1] = entries[0];
    entries[1].resource_id = 8u;
    entries[1].flags = RIN_RESOURCE_CATALOG_SOURCE_PATH |
                       RIN_RESOURCE_CATALOG_FLAG_IMMUTABLE;
    entries[1].path = "/fonts/path.psf";
    entries[1].path_size = 15u;
    entries[1].data = NULL;
    entries[1].data_size = 0u;

    memset(&catalog, 0, sizeof(catalog));
    catalog.struct_size = sizeof(catalog);
    catalog.version = RIN_RESOURCE_CATALOG_VERSION_1;
    catalog.entries = entries;
    catalog.entry_count = 2u;
    catalog.generation = 1u;

    g_aq_alloc = test_alloc;
    g_aq_free = test_free;
    font = aq_font_load_resource_psf(&catalog, 7u, NULL, NULL, storage,
                                     sizeof(storage), &storage_size);
    assert(font != NULL && storage_size == sizeof(psf1));
    assert(aq_font_lookup_glyph(font, 65u) == 65);
    aq_font_destroy(font);

    source.bytes = psf1;
    source.size = sizeof(psf1);
    source.calls = 0u;
    storage_size = UINT64_MAX;
    font = aq_font_load_resource_psf(&catalog, 8u, read_path, &source, storage,
                                     sizeof(storage), &storage_size);
    assert(font != NULL && storage_size == sizeof(psf1) && source.calls == 1u);
    aq_font_destroy(font);

    storage_size = UINT64_MAX;
    assert(aq_font_load_resource_psf(&catalog, 7u, NULL, NULL, too_small,
                                     sizeof(too_small), &storage_size) == NULL);
    assert(storage_size == 0u);

    storage_size = UINT64_MAX;
    assert(aq_font_load_resource_psf(&catalog, 7u, NULL, NULL, storage,
                                     sizeof(storage), &storage_size) != NULL);
    assert(storage_size == sizeof(psf1));
    return 0;
}
