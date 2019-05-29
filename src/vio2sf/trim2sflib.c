#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "desmume/barray.h"
#include "desmume/state.h"

#include "common.h"

void add_le32(u8 **start_ptr, u8 **current_ptr, u8 **end_ptr, u32 word) {
	if (*end_ptr - *current_ptr < 4) {
		size_t current_offset = *current_ptr - *start_ptr;
		size_t current_size = *end_ptr - *start_ptr;
		u8 *new_block = (u8 *)realloc(*start_ptr, current_size + 1024);
		if (!new_block)
			return;
		*start_ptr = new_block;
		*current_ptr = new_block + current_offset;
		*end_ptr = new_block + current_size + 1024;
	}

	(*current_ptr)[0] = word;
	(*current_ptr)[1] = word >> 8;
	(*current_ptr)[2] = word >> 16;
	(*current_ptr)[3] = word >> 24;

	*current_ptr += 4;
}

void add_block(u8 **start_ptr, u8 **current_ptr, u8 **end_ptr, const u8 *block,
               u32 size) {
	if (*end_ptr - *current_ptr < size) {
		size_t current_offset = *current_ptr - *start_ptr;
		size_t current_size = *end_ptr - *start_ptr;
		u8 *new_block =
		    (u8 *)realloc(*start_ptr, current_offset + size + 1024);
		if (!new_block)
			return;
		*start_ptr = new_block;
		*current_ptr = new_block + current_offset;
		*end_ptr = new_block + current_offset + size + 1024;
	}

	memcpy(*current_ptr, block, size);

	*current_ptr += size;
}

void add_block_zero(u8 **start_ptr, u8 **current_ptr, u8 **end_ptr, u32 size) {
	if (*end_ptr - *current_ptr < size) {
		size_t current_offset = *current_ptr - *start_ptr;
		size_t current_size = *end_ptr - *start_ptr;
		u8 *new_block =
		    (u8 *)realloc(*start_ptr, current_offset + size + 1024);
		if (!new_block)
			return;
		*start_ptr = new_block;
		*current_ptr = new_block + current_offset;
		*end_ptr = new_block + current_offset + size + 1024;
	}

	memset(*current_ptr, 0, size);

	*current_ptr += size;
}

int main(int argc, char **argv) {
	if (argc >= 3) {
		void *array_combined = NULL;
		NDS_state *core = (NDS_state *)calloc(1, sizeof(NDS_state));
		s16 buffer[2048];

		int i, j;
		for (i = 2; i < argc; ++i) {
			int total_rendered = 0;
			size_t last_bits_set = 0;
			size_t current_bits_set;
			int blocks_without_coverage = 0;
			struct twosf_loader_state state;
			memset(&state, 0, sizeof(state));
			state.initial_frames = -1;

			if (psf_load(argv[i], &stdio_callbacks, 0x24, twosf_loader,
			             &state, twosf_info, &state, 1, print_message,
			             0) <= 0) {
				if (state.rom)
					free(state.rom);
				if (state.state)
					free(state.state);
				fprintf(stderr, "Invalid 2SF file: %s\n", argv[i]);
				return 1;
			}

			if (state_init(core)) {
				state_deinit(core);
				if (state.rom)
					free(state.rom);
				if (state.state)
					free(state.state);
				fprintf(stderr, "Out of memory!\n");
				return 1;
			}

			core->dwInterpolation = 0;
			core->dwChannelMute = 0;

			if (!state.arm7_clockdown_level)
				state.arm7_clockdown_level = state.clockdown;
			if (!state.arm9_clockdown_level)
				state.arm9_clockdown_level = state.clockdown;

			core->initial_frames = state.initial_frames;
			core->sync_type = state.sync_type;
			core->arm7_clockdown_level = state.arm7_clockdown_level;
			core->arm9_clockdown_level = state.arm9_clockdown_level;

			if (state.rom)
				state_setrom(core, state.rom, (u32)state.rom_size, 1);

			state_loadstate(core, state.state, (u32)state.state_size);

			if (state.state)
				free(state.state);

			fprintf(stderr, "Clocking %s...", argv[i]);

			for (;;) {
				for (j = 0; j < 44100 * 5; j += 1024) {
					state_render(core, (s16 *)buffer, 1024);
				}
				total_rendered += j;
				current_bits_set =
				    bit_array_count(core->array_rom_coverage);
				if (current_bits_set > last_bits_set) {
					last_bits_set = current_bits_set;
					blocks_without_coverage = 0;
				} else {
					blocks_without_coverage++;
					if (blocks_without_coverage >= 6) {
						break;
					}
				}
			}

			if (!array_combined) {
				array_combined =
				    bit_array_dup(core->array_rom_coverage);
			} else {
				bit_array_merge(array_combined,
				                core->array_rom_coverage, 0);
			}

			state_deinit(core);
			if (state.rom)
				free(state.rom);

			fprintf(stderr, "ran for %d samples, covering %zu words\n",
			        total_rendered, last_bits_set);
		}

		{
			FILE *f_in, *f_out;

			char out_name[1024];

			u32 exe_size_to_skip;

			z_stream z;
			u8 zbuf[16384];
			uLong zcrc;
			uLong zlen;
			int zflush;
			int zret;

			struct twosf_loader_state state;
			memset(&state, 0, sizeof(state));

			u8 *rom_block = malloc(1024);
			u8 *rom_pointer = rom_block;
			u8 *rom_end = rom_block + 1024;

			size_t rom_bit_count = bit_array_size(array_combined);
			size_t current_bit;

			u32 last_block_end;
			u32 block_offset;
			u32 block_size = 0;

			int first_block = 1;

			if (psf_load(argv[1], &stdio_callbacks, 0x24, twosf_loader,
			             &state, twosf_info, &state, 1) <= 0) {
				if (state.rom)
					free(state.rom);
				if (state.state)
					free(state.state);
				fprintf(stderr, "Invalid 2SFLIB file: %s\n", argv[1]);
				return 1;
			}

			fprintf(stderr, "ROM size: %zu bytes\n", rom_bit_count * 4);

			for (current_bit = 0; current_bit < rom_bit_count;
			     ++current_bit) {
				if (bit_array_test(array_combined, current_bit)) {
					if (!block_size) {
						block_offset = current_bit * 4;
						block_size = 4;
					} else {
						block_size += 4;
					}
				} else {
					if (block_size) {
						if (first_block) {
							add_le32(
							    &rom_block, &rom_pointer,
							    &rom_end, block_offset);
							add_le32(&rom_block,
							         &rom_pointer,
							         &rom_end, 0);
							first_block = 0;
						} else {
							add_block_zero(
							    &rom_block, &rom_pointer,
							    &rom_end,
							    block_offset -
							        last_block_end);
						}
						add_block(&rom_block, &rom_pointer,
						          &rom_end,
						          state.rom + block_offset,
						          block_size);
						fprintf(stderr,
						        "Block from %08x for %x "
						        "bytes\n",
						        block_offset, block_size);
						last_block_end =
						    block_offset + block_size;
					}
					block_size = 0;
				}
			}

			if (block_size) {
				if (first_block) {
					add_le32(&rom_block, &rom_pointer, &rom_end,
					         block_offset);
					add_le32(&rom_block, &rom_pointer, &rom_end,
					         0);
				} else {
					add_block_zero(&rom_block, &rom_pointer,
					               &rom_end,
					               block_offset - last_block_end);
				}
				add_block(&rom_block, &rom_pointer, &rom_end,
				          state.rom + block_offset, block_size);
			}

			if (rom_pointer - rom_block - 8 < state.rom_size) {
				add_block_zero(
				    &rom_block, &rom_pointer, &rom_end,
				    state.rom_size - (rom_pointer - rom_block - 8));
			}

			set_le32(rom_block + 4, rom_pointer - rom_block - 8);

			fprintf(stderr, "New ROM uncompressed size: %zu\n",
			        rom_pointer - rom_block);

			strcpy(out_name, argv[1]);
			strcat(out_name, ".trimmed");

			f_in = fopen(argv[1], "rb");
			f_out = fopen(out_name, "wb");

			if (!f_in || !f_out) {
				fprintf(stderr, "Unable to open files for trimming "
				                "transaction!\n");
				return 1;
			}

			if (fread(out_name, 1, 8, f_in) != 8 ||
			    fwrite(out_name, 1, 8, f_out) != 8 ||
			    fwrite(out_name, 1, 8, f_out) != 8) {
				fprintf(stderr, "Unable to prepare file header!\n");
				fclose(f_out);
				fclose(f_in);
				return 1;
			}

			z.zalloc = Z_NULL;
			z.zfree = Z_NULL;
			z.opaque = Z_NULL;
			if (deflateInit(&z, Z_BEST_COMPRESSION) != Z_OK) {
				fclose(f_out);
				fclose(f_in);
				fprintf(stderr, "Unable to initialize zlib!\n");
				return 1;
			}

			z.next_in = rom_block;
			z.avail_in = (uInt)(rom_pointer - rom_block);
			z.next_out = zbuf;
			z.avail_out = 16384;
			zcrc = crc32(0L, Z_NULL, 0);

			do {
				if (z.avail_in == 0) {
					zflush = Z_FINISH;
				} else {
					zflush = Z_NO_FLUSH;
				}

				zret = deflate(&z, zflush);
				if (zret != Z_STREAM_END && zret != Z_OK) {
					deflateEnd(&z);
					fclose(f_out);
					fclose(f_in);
					return 1;
				}

				zlen = 16384 - z.avail_out;
				if (zlen != 0) {
					if (fwrite(zbuf, zlen, 1, f_out) != 1) {
						deflateEnd(&z);
						fclose(f_out);
						fclose(f_in);
						fprintf(stderr, "Unable to write "
						                "compressed data "
						                "block!\n");
						return 1;
					}
					zcrc = crc32(zcrc, zbuf, zlen);
				}

				z.next_out = zbuf;
				z.avail_out = 16384;
			} while (zret != Z_STREAM_END);

			fseek(f_out, 8, SEEK_SET);
			set_le32(out_name, z.total_out);
			set_le32(out_name + 4, zcrc);
			fwrite(out_name, 1, 8, f_out);

			fprintf(stderr, "New compressed ROM size: %zu\n", z.total_out);

			deflateEnd(&z);

			fseek(f_in, 8, SEEK_SET);
			fread(out_name, 1, 4, f_in);

			exe_size_to_skip = get_le32(out_name);

			fseek(f_in, 16 + exe_size_to_skip, SEEK_SET);

			while (!feof(f_in)) {
				size_t read = fread(zbuf, 1, 16384, f_in);
				fwrite(zbuf, 1, read, f_out);
				if (read < 16384)
					break;
			}

			fclose(f_out);
			fclose(f_in);

			fprintf(stderr, "Done!\n");
		}
	}

	return 0;
}
