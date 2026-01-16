#include "internal.h"
#include <bmacro.h>
#include <bminmax.h>
#include <string.h>

static size_t
ssync_bsv_in_read(struct bsv_in_s* in, void* buf, size_t size) {
	ssync_bsv_in_t* ssync_bsv_in = BCONTAINER_OF(in, ssync_bsv_in_t, bsv);
	return bitstream_read(ssync_bsv_in->stream, buf, size * CHAR_BIT) ? size : 0;
}

bsv_in_t*
ssync_init_bsv_in(ssync_bsv_in_t* bsv_in, bitstream_in_t* bitstream) {
	*bsv_in = (ssync_bsv_in_t){
		.bsv.read = ssync_bsv_in_read,
		.stream = bitstream,
	};
	return &bsv_in->bsv;
}

static size_t
ssync_bsv_out_write(struct bsv_out_s* out, const void* buf, size_t size) {
	ssync_bsv_out_t* ssync_bsv_out = BCONTAINER_OF(out, ssync_bsv_out_t, bsv);
	return bitstream_write(ssync_bsv_out->stream, buf, size * CHAR_BIT) ? size : 0;
}

bsv_out_t*
ssync_init_bsv_out(ssync_bsv_out_t* bsv_out, bitstream_out_t* bitstream) {
	*bsv_out = (ssync_bsv_out_t){
		.bsv.write = ssync_bsv_out_write,
		.stream = bitstream,
	};
	return &bsv_out->bsv;
}

static size_t
ssync_bsv_count_write(struct bsv_out_s* out, const void* buf, size_t size) {
	ssync_bsv_count_t* ssync_bsv_count = BCONTAINER_OF(out, ssync_bsv_count_t, bsv);
	ssync_bsv_count->count += size;
	return size;
}

bsv_out_t*
ssync_init_bsv_count(ssync_bsv_count_t* bsv_count) {
	*bsv_count = (ssync_bsv_count_t){
		.bsv.write = ssync_bsv_count_write,
	};
	return &bsv_count->bsv;
}
