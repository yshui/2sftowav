#include <locale.h>
#include <math.h>

#include "common.h"

#include "desmume/state.h"

static int16_t samples[4096];

// http://soundfile.sapp.org/doc/WaveFormat/
void write_wav_header(FILE *outf, int nsamples) {
#define w16(x) fwrite((uint16_t[]){ htole16(x) }, 1, 2, outf)
#define w32(x) fwrite((uint32_t[]){ htole32(x) }, 1, 4, outf)
	// Write wav header
	// ChunkID
	fwrite("RIFF", 4, 1, outf);
	// ChunkSize
	int subchunk2size = nsamples * 2 * 2;
	w32(subchunk2size + 36);
	// Format
	fwrite("WAVE", 4, 1, outf);

	// Subchunk1ID
	fwrite("fmt ", 4, 1, outf);
	// Subchunk1Size
	w32(16);
	// Audio format
	w16(1);
	// NChannels
	w16(2);
	// SampleRate
	w32(44100);
	// ByteRate
	w32(44100 * 2 * 2);
	// BlockAlign
	w16(2 * 2);
	// BitsPerSample
	w16(16);

	// Subchunk2ID
	fwrite("data", 4, 1, outf);
	w32(subchunk2size);
}

int main(int argc, char **argv) {
	setlocale(LC_ALL, "C");

	struct twosf_loader_state state = {0};
	if (argc != 3) {
		printf("Usage: %s <input> <output>\n", argv[0]);
		return 1;
	}
	if (psf_load(argv[1], &stdio_callbacks, 0x24, twosf_loader, &state,
	             twosf_info, &state, 1, print_message, 0) <= 0) {
		fprintf(stderr, "Failed to load 2sf: %s\n", argv[1]);
		return 1;
	}

	if (state.length_ms <= 0) {
		fprintf(stderr, "Input file has an invalid, or missing length tag, "
		                "fix it and try again\n");
		return 1;
	}

	NDS_state core = {0};
	if (state_init(&core)) {
		fprintf(stderr, "Failed to init NDS state\n");
		return 1;
	}

	fprintf(stderr, "%ld\n", state.length_ms);

	core.dwInterpolation = 0;
	core.dwChannelMute = 0;

	if (!state.arm7_clockdown_level)
		state.arm7_clockdown_level = state.clockdown;
	if (!state.arm9_clockdown_level)
		state.arm9_clockdown_level = state.clockdown;

	core.initial_frames = state.initial_frames;
	core.sync_type = state.sync_type;
	core.arm7_clockdown_level = state.arm7_clockdown_level;
	core.arm9_clockdown_level = state.arm9_clockdown_level;

	if (state.rom) {
		state_setrom(&core, state.rom, state.rom_size, 0);
	}

	state_loadstate(&core, state.state, state.state_size);

	long nsamples = state.length_ms * 441 / 10 + 1;
	long fade_samples = state.fade_ms * 441 / 10;
	long total = nsamples;

	FILE *outf = fopen(argv[2], "w");
	write_wav_header(outf, nsamples);

	int runs = 0;
	double appx_avg = 0;
	while (nsamples) {
		long to_write = 2048;
		if (nsamples < to_write) {
			to_write = nsamples;
		}

		state_render(&core, samples, to_write);

		if (nsamples > fade_samples) {
			double sum = 0;
			for (int i = 0; i < to_write * 2; i++) {
				sum += abs(samples[i]);
			}
			appx_avg = appx_avg * (44100.0 - to_write) / 44100.0 +
			           sum / 44100.0;
		} else {
			// fade out
			//double x = fade_samples / log(appx_avg);
			//double fac = exp(-(fade_samples - nsamples) / x);
			for (int i = 0; i < to_write * 2; i++) {
				double fac = (nsamples - i/2) / (double)fade_samples;
				samples[i] = (int16_t)((double)samples[i] * fac);
			}
		}

		fwrite(samples, sizeof(int16_t), to_write * 2, outf);
		nsamples -= to_write;
		if (++runs == 10) {
			fprintf(stderr, "\rRemaining: %lds/%lds", nsamples / 44100,
			        (total + 450) / 44100);
			fflush(stderr);
			runs = 0;
		}
	}
	fclose(outf);
}
