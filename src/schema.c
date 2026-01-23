#include <slopsync/schema.h>
#include <slopsync/client.h>
#include <stdlib.h>
#include <blog.h>
#include <bhash.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

void
ssync_sync_static_schema(struct ssync_s* ssync, const ssync_static_schema_t* schema) {
	size_t size = ssync_info(ssync).schema_size;
	char* content = malloc(size);
	ssync_write_schema(ssync, content);
	uint64_t hash = bhash_hash(content, size);

	if (size == schema->size && hash == schema->hash) {
		return;
	}

	BLOG_INFO("Updating ssync schema at %s", schema->filename);

	FILE* file = fopen(schema->filename, "wb");
	if (file == NULL) {
		BLOG_ERROR("Could not open file for writing: %s", strerror(errno));
		free(content);
		return;
	}

	fprintf(
		file,
		"#pragma once\n\n"
		"#include <slopsync/schema.h>\n\n"
	);

	fprintf(file, "static const ssync_static_schema_t ssync_schema = {\n");
	fprintf(file, "\t.filename = __FILE__,\n");
	fprintf(file, "\t.size = %zuull,\n", size);
	fprintf(file, "\t.hash = %" PRIu64 "ull,\n", hash);
	fprintf(file, "\t.content =\n");

	int line_width = 76;
	for (size_t i = 0; i < size; i += line_width) {
		int remaining_len = (int)size - i;
		int len = line_width < remaining_len ? line_width : remaining_len;
		fprintf(file, "\t\t\"%.*s\"\n", len, &content[i]);
	}

	fprintf(file, "};\n");

	fclose(file);
	free(content);
}
