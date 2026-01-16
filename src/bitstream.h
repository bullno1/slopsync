#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct {
	void* data;
	size_t num_bytes;
	size_t bit_pos;
} bitstream_out_t;

typedef struct {
	const void* data;
	size_t num_bytes;
	size_t bit_pos;
} bitstream_in_t;

static inline bool
bitstream_write(bitstream_out_t* stream, const uint8_t* data, size_t num_bits) {
	if (!data || num_bits == 0) {
		return num_bits == 0;
	}

	size_t bits_available = stream->num_bytes * 8 - stream->bit_pos;
	if (num_bits > bits_available) { return false; }

	uint8_t* buffer = (uint8_t*)stream->data;
	size_t bit_pos = stream->bit_pos;

	// Write bits
	for (size_t i = 0; i < num_bits; i++) {
		size_t src_byte = i / 8;
		size_t src_bit = i % 8;
		uint8_t bit = (data[src_byte] >> src_bit) & 1;

		size_t dst_byte = bit_pos / 8;
		size_t dst_bit = bit_pos % 8;

		if (bit) {
			buffer[dst_byte] |= (1 << dst_bit);
		} else {
			buffer[dst_byte] &= ~(1 << dst_bit);
		}

		bit_pos++;
	}

	stream->bit_pos = bit_pos;
	return true;
}

static inline bool
bitstream_read(bitstream_in_t* stream, uint8_t* data, size_t num_bits) {
	if (!stream || !data || num_bits == 0) {
		return num_bits == 0; // Reading 0 bits is always successful
	}

	size_t bits_available = stream->num_bytes * 8 - stream->bit_pos;
	if (num_bits > bits_available) { return false; }

	// Clear the output buffer
	size_t output_bytes = (num_bits + 7) / 8;
	memset(data, 0, output_bytes);

	const uint8_t* buffer = (const uint8_t*)stream->data;
	size_t bit_pos = stream->bit_pos;

	for (size_t i = 0; i < num_bits; i++) {
		size_t src_byte = bit_pos / 8;
		size_t src_bit = bit_pos % 8;
		uint8_t bit = (buffer[src_byte] >> src_bit) & 1;

		size_t dst_byte = i / 8;
		size_t dst_bit = i % 8;

		if (bit) {
			data[dst_byte] |= (1 << dst_bit);
		}

		bit_pos++;
	}

	stream->bit_pos = bit_pos;
	return true;
}

static inline bool
bitstream_append(bitstream_out_t* dst, const bitstream_out_t* src) {
	return bitstream_write(dst, src->data, src->bit_pos);
}

#endif
