#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <zlib.h>

#include "../psflib/psflib.h"

typedef uint32_t u32;

static long parse_time(const char *input) {
	static const long factor[] = {1, 60, 60 * 60};
	long colon_count = 0;
	long value = -1;

	char *dup = strdup(input);
	char *ptr = dup;

	while (*ptr && ((*ptr >= '0' && *ptr <= '9') || *ptr == ':' || *ptr == '.' || *ptr == ',')) {
		colon_count += *ptr == ':';
		if (*ptr == ',') {
			*ptr = '.';
		}
		++ptr;
	}

	if (colon_count > 2 || *ptr) {
		goto out;
	}

	ptr = dup;
	for (int i = 0; i < colon_count + 1; i++) {
		char *end = NULL;
		if (i == colon_count) {
			value += (long)(strtod(ptr, &end) * 1000);
		} else {
			value += (long)(strtol(ptr, &end, 10) * factor[colon_count - i] * 1000);
		}
		if (*end != 0 && *end != ':') {
			value = -1;
			goto out;
		}
		ptr = strchr(ptr, ':')+1;
	}

out:
	free(dup);
	return value;
}

static void *stdio_fopen(const char *path) { return fopen(path, "rb"); }

static size_t stdio_fread(void *p, size_t size, size_t count, void *f) {
	return fread(p, size, count, (FILE *)f);
}

static int stdio_fseek(void *f, int64_t offset, int whence) {
	return fseek((FILE *)f, offset, whence);
}

static int stdio_fclose(void *f) { return fclose((FILE *)f); }

static long stdio_ftell(void *f) { return ftell((FILE *)f); }

static psf_file_callbacks stdio_callbacks = {"\\/:",      stdio_fopen,  stdio_fread,
                                             stdio_fseek, stdio_fclose, stdio_ftell};

struct twosf_loader_state {
	uint8_t *rom;
	uint8_t *state;
	size_t rom_size;
	size_t state_size;
	long length_ms;
	long fade_ms;

	int initial_frames;
	int sync_type;
	int clockdown;
	int arm9_clockdown_level;
	int arm7_clockdown_level;
};

unsigned get_le32(void const *p) {
	return (unsigned)((unsigned char const *)p)[3] << 24 |
	       (unsigned)((unsigned char const *)p)[2] << 16 |
	       (unsigned)((unsigned char const *)p)[1] << 8 |
	       (unsigned)((unsigned char const *)p)[0];
}

void set_le32(void *p, u32 word) {
	unsigned char *_p = (unsigned char *)p;
	_p[0] = word;
	_p[1] = word >> 8;
	_p[2] = word >> 16;
	_p[3] = word >> 24;
}

static int load_twosf_map(struct twosf_loader_state *state, int issave,
                          const unsigned char *udata, unsigned usize) {
	if (usize < 8)
		return -1;

	unsigned char *iptr;
	size_t isize;
	unsigned char *xptr;
	unsigned xsize = get_le32(udata + 4);
	unsigned xofs = get_le32(udata + 0);
	if (issave) {
		iptr = state->state;
		isize = state->state_size;
		state->state = 0;
		state->state_size = 0;
	} else {
		iptr = state->rom;
		isize = state->rom_size;
		state->rom = 0;
		state->rom_size = 0;
	}
	if (!iptr) {
		size_t rsize = xofs + xsize;
		if (!issave) {
			rsize -= 1;
			rsize |= rsize >> 1;
			rsize |= rsize >> 2;
			rsize |= rsize >> 4;
			rsize |= rsize >> 8;
			rsize |= rsize >> 16;
			rsize += 1;
		}
		iptr = (unsigned char *)malloc(rsize + 10);
		if (!iptr)
			return -1;
		memset(iptr, 0, rsize + 10);
		isize = rsize;
	} else if (isize < xofs + xsize) {
		size_t rsize = xofs + xsize;
		if (!issave) {
			rsize -= 1;
			rsize |= rsize >> 1;
			rsize |= rsize >> 2;
			rsize |= rsize >> 4;
			rsize |= rsize >> 8;
			rsize |= rsize >> 16;
			rsize += 1;
		}
		xptr = (unsigned char *)realloc(iptr, xofs + rsize + 10);
		if (!xptr) {
			free(iptr);
			return -1;
		}
		iptr = xptr;
		isize = rsize;
	}
	memcpy(iptr + xofs, udata + 8, xsize);
	if (issave) {
		state->state = iptr;
		state->state_size = isize;
	} else {
		state->rom = iptr;
		state->rom_size = isize;
	}
	return 0;
}

static int load_twosf_mapz(struct twosf_loader_state *state, int issave,
                           const unsigned char *zdata, unsigned zsize, unsigned zcrc) {
	int ret;
	int zerr;
	uLongf usize = 8;
	uLongf rsize = usize;
	unsigned char *udata;
	unsigned char *rdata;

	udata = (unsigned char *)malloc(usize);
	if (!udata)
		return -1;

	while (Z_OK != (zerr = uncompress(udata, &usize, zdata, zsize))) {
		if (Z_MEM_ERROR != zerr && Z_BUF_ERROR != zerr) {
			free(udata);
			return -1;
		}
		if (usize >= 8) {
			usize = get_le32(udata + 4) + 8;
			if (usize < rsize) {
				rsize += rsize;
				usize = rsize;
			} else
				rsize = usize;
		} else {
			rsize += rsize;
			usize = rsize;
		}
		rdata = (unsigned char *)realloc(udata, usize);
		if (!rdata) {
			free(udata);
			return -1;
		}
		udata = rdata;
	}

	rdata = (unsigned char *)realloc(udata, usize);
	if (!rdata) {
		free(udata);
		return -1;
	}

	if (0) {
		uLong ccrc = crc32(crc32(0L, Z_NULL, 0), rdata, (uInt)usize);
		if (ccrc != zcrc)
			return -1;
	}

	ret = load_twosf_map(state, issave, rdata, (unsigned)usize);
	free(rdata);
	return ret;
}

static int twosf_loader(void *context, const uint8_t *exe, size_t exe_size,
                        const uint8_t *reserved, size_t reserved_size) {
	struct twosf_loader_state *state = (struct twosf_loader_state *)context;

	if (exe_size >= 8) {
		if (load_twosf_map(state, 0, exe, (unsigned)exe_size))
			return -1;
	}

	if (reserved_size) {
		size_t resv_pos = 0;
		if (reserved_size < 16)
			return -1;
		while (resv_pos + 12 < reserved_size) {
			unsigned save_size = get_le32(reserved + resv_pos + 4);
			unsigned save_crc = get_le32(reserved + resv_pos + 8);
			if (get_le32(reserved + resv_pos + 0) == 0x45564153) {
				if (resv_pos + 12 + save_size > reserved_size)
					return -1;
				if (load_twosf_mapz(state, 1, reserved + resv_pos + 12,
				                    save_size, save_crc))
					return -1;
			}
			resv_pos += 12 + save_size;
		}
	}

	return 0;
}

static int twosf_info(void *context, const char *name, const char *value) {
	struct twosf_loader_state *state = (struct twosf_loader_state *)context;
	char *end;

	fprintf(stderr, "%s: %s\n", name, value);

	if (!strcasecmp(name, "_frames")) {
		state->initial_frames = strtoul(value, &end, 10);
	} else if (!strcasecmp(name, "_clockdown")) {
		state->clockdown = strtoul(value, &end, 10);
	} else if (!strcasecmp(name, "_vio2sf_sync_type")) {
		state->sync_type = strtoul(value, &end, 10);
	} else if (!strcasecmp(name, "_vio2sf_arm9_clockdown_level")) {
		state->arm9_clockdown_level = strtoul(value, &end, 10);
	} else if (!strcasecmp(name, "_vio2sf_arm7_clockdown_level")) {
		state->arm7_clockdown_level = strtoul(value, &end, 10);
	} else if (!strcasecmp(name, "length")) {
		state->length_ms = parse_time(value);
	} else if (!strcasecmp(name, "fade")) {
		state->fade_ms = parse_time(value);
	}

	return 0;
}

static void print_message(void *unused, const char *message) {
	fputs(message, stderr);
}
