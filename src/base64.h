#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static const char base64_chars[64] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
	'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
	'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
	'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

static const int8_t base64_inv[256] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
	-1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
	-1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static inline size_t
base64_encoded_size(size_t input_len) {
	size_t full_groups = input_len / 3;
	size_t remainder = input_len % 3;
	if (remainder == 0) {
		return full_groups * 4;
	} else if (remainder == 1) {
		return full_groups * 4 + 2;
	} else {
		return full_groups * 4 + 3;
	}
}

static inline void
base64_encode(const uint8_t* input, size_t input_len, char* output) {
	size_t i = 0, j = 0;
	while (i + 3 <= input_len) {
		uint32_t val = (input[i] << 16) | (input[i+1] << 8) | input[i+2];
		output[j]   = base64_chars[(val >> 18) & 0x3F];
		output[j+1] = base64_chars[(val >> 12) & 0x3F];
		output[j+2] = base64_chars[(val >> 6)  & 0x3F];
		output[j+3] = base64_chars[val & 0x3F];
		i += 3;
		j += 4;
	}

	size_t remainder = input_len - i;
	if (remainder == 1) {
		uint32_t val = input[i] << 16;
		output[j]   = base64_chars[(val >> 18) & 0x3F];
		output[j+1] = base64_chars[(val >> 12) & 0x3F];
	} else if (remainder == 2) {
		uint32_t val = (input[i] << 16) | (input[i+1] << 8);
		output[j]   = base64_chars[(val >> 18) & 0x3F];
		output[j+1] = base64_chars[(val >> 12) & 0x3F];
		output[j+2] = base64_chars[(val >> 6)  & 0x3F];
	}
}

static inline size_t
base64_decoded_size(size_t input_len) {
	size_t full_groups = input_len / 4;
	size_t remainder = input_len % 4;
	if (remainder == 0) {
		return full_groups * 3;
	} else if (remainder == 1) {
		return 0;
	} else if (remainder == 2) {
		return full_groups * 3 + 1;
	} else { // 3
		return full_groups * 3 + 2;
	}
}

static inline bool
base64_decode(const char* input, size_t input_len, char* output) {
	if (input_len % 4 == 1) { return false; }

	size_t i = 0, j = 0;
	while (i + 4 <= input_len) {
		int8_t idx0 = base64_inv[(unsigned char)input[i]];
		int8_t idx1 = base64_inv[(unsigned char)input[i+1]];
		int8_t idx2 = base64_inv[(unsigned char)input[i+2]];
		int8_t idx3 = base64_inv[(unsigned char)input[i+3]];
		if (idx0 < 0 || idx1 < 0 || idx2 < 0 || idx3 < 0) {
			return false;
		}
		uint32_t val = (idx0 << 18) | (idx1 << 12) | (idx2 << 6) | idx3;
		output[j]   = (val >> 16) & 0xFF;
		output[j+1] = (val >> 8)  & 0xFF;
		output[j+2] = val         & 0xFF;
		i += 4;
		j += 3;
	}

	size_t remainder = input_len - i;
	if (remainder == 2) {
		int8_t idx0 = base64_inv[(unsigned char)input[i]];
		int8_t idx1 = base64_inv[(unsigned char)input[i+1]];
		if (idx0 < 0 || idx1 < 0) {
			return false;
		}
		uint32_t val = (idx0 << 18) | (idx1 << 12);
		output[j] = (val >> 16) & 0xFF;
		// Check padding bits
		if ((val & 0xFFF) != 0) { // Bottom 12 bits should be 0, but since only 8 bits used, bottom 4 of idx1 should be 0
			return false;
		}
	} else if (remainder == 3) {
		int8_t idx0 = base64_inv[(unsigned char)input[i]];
		int8_t idx1 = base64_inv[(unsigned char)input[i+1]];
		int8_t idx2 = base64_inv[(unsigned char)input[i+2]];
		if (idx0 < 0 || idx1 < 0 || idx2 < 0) {
			return false;
		}
		uint32_t val = (idx0 << 18) | (idx1 << 12) | (idx2 << 6);
		output[j]   = (val >> 16) & 0xFF;
		output[j+1] = (val >> 8)  & 0xFF;
		// Check padding bits
		if ((val & 0x3F) != 0) { // Bottom 6 bits should be 0, bottom 2 of idx2
			return false; // Malformed
		}
	}

	return true;
}

#endif
