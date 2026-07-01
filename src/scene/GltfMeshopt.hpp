#pragma once

#include <cgltf.h>

namespace saida {

// Decodes every EXT_meshopt_compression buffer view in `data`, pointing each
// view->data at freshly decoded bytes (cgltf_buffer_view_data() then returns the
// decoded view transparently, so accessor reads work unchanged). Returns false
// on a decode error.
//
// The decoded buffers are allocated with cgltf's own allocator and handed to
// `data` via view->data, so cgltf_free(data) releases them at the right time —
// the caller just keeps `data` alive until done reading, then frees it as usual.
//
// This is the runtime decode half of the web asset pipeline (Étape 16): assets
// ship as meshopt-compressed GLB and are decoded on load. Shared by GLTFLoader
// and the exporter's round-trip test.
bool decodeMeshoptBuffers(cgltf_data* data);

} // namespace saida
