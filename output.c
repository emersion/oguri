#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "oguri.h"
#include "output.h"
#include "buffers.h"

static void noop() {}  // For unused listener members.

//
// Wayland outputs
//

static void handle_output_scale(
		void * data,
		struct wl_output * output __attribute__((unused)),
		int32_t factor) {
	((struct oguri_output *) data)->scale = factor;
}

struct wl_output_listener output_listener = {
	.scale = handle_output_scale,
	.geometry = noop,
	.mode = noop,
	.done = noop,
};

//
// wlroots layer surface
//

static void layer_surface_configure(
		void * data,
		struct zwlr_layer_surface_v1 * layer_surface,
		uint32_t serial,
		uint32_t width,
		uint32_t height) {
	struct oguri_output * output = data;

	output->width = width;
	output->height = height;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

	if (!oguri_allocate_buffers(output)) {
		fprintf(stderr, "No buffers, woe is me\n");
	}
}

static void layer_surface_closed(
		void * data,
		struct zwlr_layer_surface_v1 * layer_surface __attribute__((unused))) {
	oguri_output_destroy((struct oguri_output *) data);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

//
// Outputs
//

struct oguri_output * oguri_output_create(
		struct oguri_state * oguri, struct wl_output * wl_output) {
	struct oguri_output * output = calloc(1, sizeof(struct oguri_output));
	wl_list_init(&output->buffer_ring);

	output->output = wl_output;
	wl_output_add_listener(wl_output, &output_listener, output);

	output->shm = oguri->shm;

	output->surface = wl_compositor_create_surface(oguri->compositor);
	assert(output->surface);

	output->input_region = wl_compositor_create_region(oguri->compositor);
	assert(output->input_region);
	wl_surface_set_input_region(output->surface, output->input_region);

	output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			oguri->layer_shell,
			output->surface,
			output->output,
			ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
			"wallpaper");

	assert(output->layer_surface);

	zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);
	zwlr_layer_surface_v1_set_anchor(output->layer_surface,
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
	zwlr_layer_surface_v1_add_listener(output->layer_surface,
			&layer_surface_listener, output);
	wl_surface_commit(output->surface);

	wl_display_roundtrip(oguri->display);

	wl_list_insert(oguri->idle_outputs.prev, &output->link);
	return output;
}

void oguri_output_destroy(struct oguri_output * output) {
	wl_list_remove(&output->link);

	free(output->name);

	wl_surface_destroy(output->surface);
	wl_region_destroy(output->input_region);
	zwlr_layer_surface_v1_destroy(output->layer_surface);

	output->shm = NULL;  // This cannot be freed here.

	struct oguri_buffer * buffer, * tmp;
	wl_list_for_each_safe(buffer, tmp, &output->buffer_ring, link) {
		wl_list_remove(&buffer->link);
		// TODO: oguri_buffer_destroy
	}

	free(output);
}