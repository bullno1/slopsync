#include "internal.h"
#include <bmacro.h>
#include <bminmax.h>
#include <string.h>

static size_t
ssync_bsv_in_read(struct bsv_in_s* in, void* buf, size_t size) {
	ssync_bsv_in_t* ssync_bsv_in = BCONTAINER_OF(in, ssync_bsv_in_t, bsv);
	const char* begin = ssync_bsv_in->blob.data;
	const char* end = begin + ssync_bsv_in->blob.size;
	size_t bytes_left = (size_t)(end - ssync_bsv_in->pos);
	size_t bytes_to_read = BMIN(bytes_left, size);
	memcpy(buf, ssync_bsv_in->pos, bytes_to_read);
	ssync_bsv_in->pos += bytes_to_read;
	return bytes_to_read;
}

bsv_in_t*
ssync_init_bsv_in(ssync_bsv_in_t* bsv_in, ssync_blob_t blob) {
	*bsv_in = (ssync_bsv_in_t){
		.blob = blob,
		.pos = blob.data,
		.bsv.read = ssync_bsv_in_read,
	};
	return &bsv_in->bsv;
}

bool
ssync_bsv_in_eof(ssync_bsv_in_t* bsv_in) {
	const char* begin = bsv_in->blob.data;
	const char* end = begin + bsv_in->blob.size;
	return bsv_in->pos >= end;
}

static size_t
ssync_bsv_out_write(struct bsv_out_s* out, const void* buf, size_t size) {
	ssync_bsv_out_t* ssync_bsv_out = BCONTAINER_OF(out, ssync_bsv_out_t, bsv);
	size_t bytes_left = (size_t)(ssync_bsv_out->buf->end - ssync_bsv_out->buf->current);
	size_t bytes_to_write = BMIN(bytes_left, size);
	memcpy(ssync_bsv_out->buf->current, buf, bytes_to_write);
	ssync_bsv_out->buf->current += bytes_to_write;
	return bytes_to_write;
}

bsv_out_t*
ssync_init_bsv_out(ssync_bsv_out_t* bsv_out, ssync_write_buf_t* buf) {
	*bsv_out = (ssync_bsv_out_t){
		.buf = buf,
		.bsv.write = ssync_bsv_out_write,
	};
	return &bsv_out->bsv;
}
