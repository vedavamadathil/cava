#include <locale.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#else
#include <stdlib.h>
#endif

#include <fcntl.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.1415926535897932385
#endif

#define MAX_BARS 1024

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

#include <ctype.h>
#include <dirent.h>
#include <fftw3.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#include "debug.h"
#include "util.h"

#include "output/raw.h"
#include "output/terminal_noncurses.h"

#include "display/init.h"

#include "input/alsa.h"
#include "input/common.h"
#include "input/fifo.h"
#include "input/portaudio.h"
#include "input/pulse.h"
#include "input/shmem.h"
#include "input/sndio.h"

#ifdef __GNUC__
// curses.h or other sources may already define
#undef GCC_UNUSED
#define GCC_UNUSED __attribute__((unused))
#else
#define GCC_UNUSED /* nothing */
#endif

pthread_mutex_t lock;
// used by sig handler
// needs to know output mode in order to clean up terminal
int output_mode;
// whether we should reload the config or not
int should_reload = 0;
// whether we should only reload colors or not
int reload_colors = 0;
// whether we should quit
int should_quit = 0;

// these variables are used only in main, but making them global
// will allow us to not free them on exit without ASan complaining
struct config_params p;

fftw_complex *out_bass_l, *out_bass_r;
fftw_plan p_bass_l, p_bass_r;
fftw_complex *out_mid_l, *out_mid_r;
fftw_plan p_mid_l, p_mid_r;
fftw_complex *out_treble_l, *out_treble_r;
fftw_plan p_treble_l, p_treble_r;

// general: cleanup
void cleanup(void) {
	if (output_mode == OUTPUT_NCURSES) {
#ifdef NCURSES
		cleanup_terminal_ncurses();
#else
		;
#endif
	} else if (output_mode == OUTPUT_NONCURSES) {
		cleanup_terminal_noncurses();
	} else if (output_mode == OUTPUT_SDL) {
#ifdef SDL
		cleanup_sdl();
#else
		;
#endif
	}
}

// general: handle signals
void sig_handler(int sig_no) {
	if (sig_no == SIGUSR1) {
		should_reload = 1;
		return;
	}

	if (sig_no == SIGUSR2) {
		reload_colors = 1;
		return;
	}

	cleanup();
	if (sig_no == SIGINT) {
		printf("CTRL-C pressed -- goodbye\n");
	}
	signal(sig_no, SIG_DFL);
	raise(sig_no);
}

#ifdef ALSA
static bool is_loop_device_for_sure(const char *text) {
	const char *const LOOPBACK_DEVICE_PREFIX = "hw:Loopback,";
	return strncmp(text, LOOPBACK_DEVICE_PREFIX, strlen(LOOPBACK_DEVICE_PREFIX)) == 0;
}

static bool directory_exists(const char *path) {
	DIR *const dir = opendir(path);
	if (dir == NULL)
		return false;

	closedir(dir);
	return true;
}

#endif

int *monstercat_filter(int *bars, int number_of_bars, int waves, double monstercat) {

	int z;

	// process [smoothing]: monstercat-style "average"

	int m_y, de;
	if (waves > 0) {
		for (z = 0; z < number_of_bars; z++) { // waves
			bars[z] = bars[z] / 1.25;
			// if (bars[z] < 1) bars[z] = 1;
			for (m_y = z - 1; m_y >= 0; m_y--) {
				de = z - m_y;
				bars[m_y] = max(bars[z] - pow(de, 2), bars[m_y]);
			}
			for (m_y = z + 1; m_y < number_of_bars; m_y++) {
				de = m_y - z;
				bars[m_y] = max(bars[z] - pow(de, 2), bars[m_y]);
			}
		}
	} else if (monstercat > 0) {
		for (z = 0; z < number_of_bars; z++) {
			// if (bars[z] < 1)bars[z] = 1;
			for (m_y = z - 1; m_y >= 0; m_y--) {
				de = z - m_y;
				bars[m_y] = max(bars[z] / pow(monstercat, de), bars[m_y]);
			}
			for (m_y = z + 1; m_y < number_of_bars; m_y++) {
				de = m_y - z;
				bars[m_y] = max(bars[z] / pow(monstercat, de), bars[m_y]);
			}
		}
	}

	return bars;
}

// general: entry point
int main()
{
	// general: console title
	// printf("%c]0;%s%c", '\033', PACKAGE, '\007');

	// general: handle command-line arguments
	char configPath[PATH_MAX];
	configPath[0] = '\0';

	/* general: handle Ctrl+C
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = &sig_handler;
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGUSR1, &action, NULL);
	sigaction(SIGUSR2, &action, NULL);

	char *usage = "\n\
		       Usage : " PACKAGE " [options]\n\
		       Visualize audio input in terminal. \n\
		       \n\
		       Options:\n\
		       -p          path to config file\n\
		       -v          print version\n\
		       \n\
		       Keys:\n\
		       Up        Increase sensitivity\n\
		       Down      Decrease sensitivity\n\
		       Left      Decrease number of bars\n\
		       Right     Increase number of bars\n\
		       r         Reload config\n\
		       c         Reload colors only\n\
		       f         Cycle foreground color\n\
		       b         Cycle background color\n\
		       q         Quit\n\
		       \n\
		       as of 0.4.0 all options are specified in config file, see in '/home/username/.config/cava/' \n";

	int c;
	while ((c = getopt(argc, argv, "p:vh")) != -1) {
		switch (c) {
			case 'p': // argument: fifo path
				snprintf(configPath, sizeof(configPath), "%s", optarg);
				break;
			case 'h': // argument: print usage
				printf("%s", usage);
				return 1;
			case '?': // argument: print usage
				printf("%s", usage);
				return 1;
			case 'v': // argument: print version
				printf(PACKAGE " " VERSION "\n");
				return 0;
			default: // argument: no arguments; exit
				abort();
		}
	} */

	// Initialize display
	struct Display *display = NULL;
	display = make_display(800, 800, "TURBAVIS");

	// general: main loop
	// TODO: split and clean up code
	while (1) {
		// TODO: remove all config stuff
		debug("loading config\n");
		// config: load
		struct error_s error;
		error.length = 0;
		if (!load_config(configPath, &p, 0, &error)) {
			fprintf(stderr, "Error loading config. %s", error.message);
			exit(EXIT_FAILURE);
		}

		bool first = true;
		int inAtty;

		output_mode = p.output;

		if (output_mode != OUTPUT_RAW) {
			// Check if we're running in a tty
			inAtty = 0;
			if (strncmp(ttyname(0), "/dev/tty", 8) == 0 || strcmp(ttyname(0), "/dev/console") == 0)
				inAtty = 1;

			// in macos vitual terminals are called ttys(xyz) and there are no ttys
			if (strncmp(ttyname(0), "/dev/ttys", 9) == 0)
				inAtty = 0;
			if (inAtty) {
				// checking if cava psf font is installed in FONTDIR
				FILE *font_file;
				font_file = fopen(FONTDIR "/cava.psf", "r");
				if (font_file) {
					fclose(font_file);
					system("setfont " FONTDIR "/cava.psf  >/dev/null 2>&1");
				} else {
					// if not it might still be available, we dont know, must try
					system("setfont cava.psf  >/dev/null 2>&1");
				}
				system("setterm -blank 0");
			}

			// We use unicode block characters to draw the bars and
			// the locale var LANG must be set to use unicode chars.
			// For some reason this var can't be retrieved with
			// setlocale(LANG, NULL), so we get it with getenv.
			// Also we can't set it with setlocale(LANG "") so we
			// must set LC_ALL instead.
			// Attempting to set to en_US if not set, if that lang
			// is not installed and LANG is not set there will be
			// no output, for more info see #109 #344
			if (!getenv("LANG"))
				setlocale(LC_ALL, "en_US.utf8");
			else
				setlocale(LC_ALL, "");
		}

		// input: init
		int *bars_left, *bars_right;
		double *temp_l, *temp_r;

		int bass_cut_off = 150;
		int treble_cut_off = 2500;

		struct audio_data audio;
		memset(&audio, 0, sizeof(audio));

		audio.source = malloc(1 + strlen(p.audio_source));
		strcpy(audio.source, p.audio_source);

		audio.format = -1;
		audio.rate = 0;
		audio.FFTbassbufferSize = MAX_BARS * 4;
		audio.FFTmidbufferSize = MAX_BARS * 2;
		audio.FFTtreblebufferSize = MAX_BARS;
		audio.terminate = 0;
		if (p.stereo)
			audio.channels = 2;
		if (!p.stereo)
			audio.channels = 1;
		audio.average = false;
		audio.left = false;
		audio.right = false;
		if (strcmp(p.mono_option, "average") == 0)
			audio.average = true;
		if (strcmp(p.mono_option, "left") == 0)
			audio.left = true;
		if (strcmp(p.mono_option, "right") == 0)
			audio.right = true;
		audio.bass_index = 0;
		audio.mid_index = 0;
		audio.treble_index = 0;
		audio.bass_multiplier = (double *)malloc(audio.FFTbassbufferSize * sizeof(double));
		audio.mid_multiplier = (double *)malloc(audio.FFTmidbufferSize * sizeof(double));
		audio.treble_multiplier = (double *)malloc(audio.FFTtreblebufferSize * sizeof(double));

		temp_l = (double *)malloc((audio.FFTbassbufferSize / 2 + 1) * sizeof(double));
		temp_r = (double *)malloc((audio.FFTbassbufferSize / 2 + 1) * sizeof(double));

		bars_left = (int *)malloc(MAX_BARS * sizeof(int));
		bars_right = (int *)malloc(MAX_BARS * sizeof(int));

		for (int i = 0; i < audio.FFTbassbufferSize; i++) {
			audio.bass_multiplier[i] =
				0.5 * (1 - cos(2 * M_PI * i / (audio.FFTbassbufferSize - 1)));
		}
		for (int i = 0; i < audio.FFTmidbufferSize; i++) {
			audio.mid_multiplier[i] = 0.5 * (1 - cos(2 * M_PI * i / (audio.FFTmidbufferSize - 1)));
		}
		for (int i = 0; i < audio.FFTtreblebufferSize; i++) {
			audio.treble_multiplier[i] =
				0.5 * (1 - cos(2 * M_PI * i / (audio.FFTtreblebufferSize - 1)));
		}
		// BASS
		// audio.FFTbassbufferSize =  audio.rate / 20; // audio.FFTbassbufferSize;

		audio.in_bass_r = fftw_alloc_real(audio.FFTbassbufferSize);
		audio.in_bass_l = fftw_alloc_real(audio.FFTbassbufferSize);
		audio.in_bass_r_raw = fftw_alloc_real(audio.FFTbassbufferSize);
		audio.in_bass_l_raw = fftw_alloc_real(audio.FFTbassbufferSize);

		out_bass_l = fftw_alloc_complex(audio.FFTbassbufferSize / 2 + 1);
		out_bass_r = fftw_alloc_complex(audio.FFTbassbufferSize / 2 + 1);
		memset(out_bass_l, 0, (audio.FFTbassbufferSize / 2 + 1) * sizeof(fftw_complex));
		memset(out_bass_r, 0, (audio.FFTbassbufferSize / 2 + 1) * sizeof(fftw_complex));

		p_bass_l = fftw_plan_dft_r2c_1d(audio.FFTbassbufferSize, audio.in_bass_l, out_bass_l,
				FFTW_MEASURE);
		p_bass_r = fftw_plan_dft_r2c_1d(audio.FFTbassbufferSize, audio.in_bass_r, out_bass_r,
				FFTW_MEASURE);

		// MID
		// audio.FFTmidbufferSize =  audio.rate / bass_cut_off; // audio.FFTbassbufferSize;
		audio.in_mid_r = fftw_alloc_real(audio.FFTmidbufferSize);
		audio.in_mid_l = fftw_alloc_real(audio.FFTmidbufferSize);
		audio.in_mid_r_raw = fftw_alloc_real(audio.FFTmidbufferSize);
		audio.in_mid_l_raw = fftw_alloc_real(audio.FFTmidbufferSize);

		out_mid_l = fftw_alloc_complex(audio.FFTmidbufferSize / 2 + 1);
		out_mid_r = fftw_alloc_complex(audio.FFTmidbufferSize / 2 + 1);
		memset(out_mid_l, 0, (audio.FFTmidbufferSize / 2 + 1) * sizeof(fftw_complex));
		memset(out_mid_r, 0, (audio.FFTmidbufferSize / 2 + 1) * sizeof(fftw_complex));

		p_mid_l =
			fftw_plan_dft_r2c_1d(audio.FFTmidbufferSize, audio.in_mid_l, out_mid_l, FFTW_MEASURE);
		p_mid_r =
			fftw_plan_dft_r2c_1d(audio.FFTmidbufferSize, audio.in_mid_r, out_mid_r, FFTW_MEASURE);

		// TRIEBLE
		// audio.FFTtreblebufferSize =  audio.rate / treble_cut_off; // audio.FFTbassbufferSize;
		audio.in_treble_r = fftw_alloc_real(audio.FFTtreblebufferSize);
		audio.in_treble_l = fftw_alloc_real(audio.FFTtreblebufferSize);
		audio.in_treble_r_raw = fftw_alloc_real(audio.FFTtreblebufferSize);
		audio.in_treble_l_raw = fftw_alloc_real(audio.FFTtreblebufferSize);

		out_treble_l = fftw_alloc_complex(audio.FFTtreblebufferSize / 2 + 1);
		out_treble_r = fftw_alloc_complex(audio.FFTtreblebufferSize / 2 + 1);
		memset(out_treble_l, 0, (audio.FFTtreblebufferSize / 2 + 1) * sizeof(fftw_complex));
		memset(out_treble_r, 0, (audio.FFTtreblebufferSize / 2 + 1) * sizeof(fftw_complex));

		p_treble_l = fftw_plan_dft_r2c_1d(audio.FFTtreblebufferSize, audio.in_treble_l,
				out_treble_l, FFTW_MEASURE);
		p_treble_r = fftw_plan_dft_r2c_1d(audio.FFTtreblebufferSize, audio.in_treble_r,
				out_treble_r, FFTW_MEASURE);

		debug("got buffer size: %d, %d, %d", audio.FFTbassbufferSize, audio.FFTmidbufferSize,
				audio.FFTtreblebufferSize);

		reset_output_buffers(&audio);

		debug("starting audio thread\n");

		pthread_t p_thread;
		int timeout_counter = 0;
		int total_bar_height = 0;

		struct timespec timeout_timer = {.tv_sec = 0, .tv_nsec = 1000000};
		int thr_id GCC_UNUSED;

		switch (p.input) {
#ifdef ALSA
			case INPUT_ALSA:
				// input_alsa: wait for the input to be ready
				if (is_loop_device_for_sure(audio.source)) {
					if (directory_exists("/sys/")) {
						if (!directory_exists("/sys/module/snd_aloop/")) {
							cleanup();
							fprintf(stderr,
									"Linux kernel module \"snd_aloop\" does not seem to  be loaded.\n"
									"Maybe run \"sudo modprobe snd_aloop\".\n");
							exit(EXIT_FAILURE);
						}
					}
				}

				thr_id = pthread_create(&p_thread, NULL, input_alsa,
						(void *)&audio); // starting alsamusic listener

				timeout_counter = 0;

				while (audio.format == -1 || audio.rate == 0) {
					nanosleep(&timeout_timer, NULL);
					timeout_counter++;
					if (timeout_counter > 2000) {
						cleanup();
						fprintf(stderr, "could not get rate and/or format, problems with audio thread? "
								"quiting...\n");
						exit(EXIT_FAILURE);
					}
				}
				debug("got format: %d and rate %d\n", audio.format, audio.rate);
				break;
#endif
			case INPUT_FIFO:
				// starting fifomusic listener
				thr_id = pthread_create(&p_thread, NULL, input_fifo, (void *)&audio);
				audio.rate = p.fifoSample;
				audio.format = p.fifoSampleBits;
				break;
#ifdef PULSE
			case INPUT_PULSE:
				if (strcmp(audio.source, "auto") == 0) {
					getPulseDefaultSink((void *)&audio);
				}
				// starting pulsemusic listener
				thr_id = pthread_create(&p_thread, NULL, input_pulse, (void *)&audio);
				audio.rate = 44100;
				break;
#endif
#ifdef SNDIO
			case INPUT_SNDIO:
				thr_id = pthread_create(&p_thread, NULL, input_sndio, (void *)&audio);
				audio.rate = 44100;
				break;
#endif
			case INPUT_SHMEM:
				thr_id = pthread_create(&p_thread, NULL, input_shmem, (void *)&audio);

				timeout_counter = 0;
				while (audio.rate == 0) {
					nanosleep(&timeout_timer, NULL);
					timeout_counter++;
					if (timeout_counter > 2000) {
						cleanup();
						fprintf(stderr, "could not get rate and/or format, problems with audio thread? "
								"quiting...\n");
						exit(EXIT_FAILURE);
					}
				}
				debug("got format: %d and rate %d\n", audio.format, audio.rate);
				// audio.rate = 44100;
				break;
#ifdef PORTAUDIO
			case INPUT_PORTAUDIO:
				thr_id = pthread_create(&p_thread, NULL, input_portaudio, (void *)&audio);
				audio.rate = 44100;
				break;
#endif
			default:
				exit(EXIT_FAILURE); // Can't happen.
		}

		if (p.upper_cut_off > audio.rate / 2) {
			cleanup();
			fprintf(stderr, "higher cuttoff frequency can't be higher than sample rate / 2");
			exit(EXIT_FAILURE);
		}

		int bars[MAX_BARS];
		int bars_mem[MAX_BARS];
		int bars_last[MAX_BARS];
		int previous_frame[MAX_BARS];
		int fall[MAX_BARS];
		float bars_peak[MAX_BARS];

		int height, lines, width, remainder, fp;

		bool reloadConf = false;
		while (!reloadConf) { // jumping back to this loop means that you resized the screen
			for (int n = 0; n < MAX_BARS; n++) {
				bars_last[n] = 0;
				previous_frame[n] = 0;
				fall[n] = 0;
				bars_peak[n] = 0;
				bars_mem[n] = 0;
				bars[n] = 0;
			}

			// frequencies on x axis require a bar width of four or more
			if (p.xaxis == FREQUENCY && p.bar_width < 4)
				p.bar_width = 4;

			// TODO: figure out source of screen clear
			output_mode = OUTPUT_NONCURSES;
			get_terminal_dim_noncurses(&width, &lines);
			printf("width %d lines %d\n", width, lines);

			if (p.xaxis != NONE)
				lines--;

			// init_terminal_noncurses(inAtty, p.col, p.bgcol, width, lines, p.bar_width);
			height = lines * 8;

			// handle for user setting too many bars
			if (p.fixedbars) {
				p.autobars = 0;
				if (p.fixedbars * p.bar_width + p.fixedbars * p.bar_spacing - p.bar_spacing > width)
					p.autobars = 1;
			}

			// getting numbers of bars
			// int number_of_bars = p.fixedbars;
			// TODO: replace this fixed
			int number_of_bars = 100;

			// if (p.autobars == 1)
			//	number_of_bars = (width + p.bar_spacing) / (p.bar_width + p.bar_spacing);

			if (number_of_bars < 1)
				number_of_bars = 1; // must have at least 1 bars
			if (number_of_bars > MAX_BARS)
				number_of_bars = MAX_BARS; // cant have more than MAX_BARS bars

			if (p.stereo) { // stereo must have even numbers of bars
				if (number_of_bars % 2 != 0)
					number_of_bars--;
			}

			// checks if there is stil extra room, will use this to center
			remainder = (width - number_of_bars * p.bar_width - number_of_bars * p.bar_spacing +
					p.bar_spacing) /
				2;
			if (remainder < 0)
				remainder = 0;

			// process [smoothing]: calculate gravity
			float g = p.gravity * log10((float)height) * 0.05 * pow((60 / (float)p.framerate), 2.5);

			if (output_mode == OUTPUT_SDL) {
				g *= 1.5; // we can assume sdl to have higher resolution so a bit more g is
				// nescescary.
			}

			// calculate integral value, must be reduced with height
			double integral = p.integral;

			// process: calculate cutoff frequencies and eq
			if (p.stereo)
				number_of_bars =
					number_of_bars / 2; // in stereo only half number of number_of_bars per channel
			// for cutoff frequencies and eq calculation

			double userEQ_keys_to_bars_ratio;

			if (p.userEQ_enabled && (number_of_bars > 0)) {
				userEQ_keys_to_bars_ratio =
					(double)(((double)p.userEQ_keys) / ((double)number_of_bars));
			}

			// calculate frequency constant (used to distribute bars across the frequency band)
			double frequency_constant = log10((float)p.lower_cut_off / (float)p.upper_cut_off) /
				(1 / ((float)number_of_bars + 1) - 1);

			float cut_off_frequency[MAX_BARS];
			float upper_cut_off_frequency[MAX_BARS];
			float relative_cut_off[MAX_BARS];
			double center_frequencies[MAX_BARS];
			int FFTbuffer_lower_cut_off[MAX_BARS];
			int FFTbuffer_upper_cut_off[MAX_BARS];
			double eq[MAX_BARS];

			int bass_cut_off_bar = -1;
			int treble_cut_off_bar = -1;
			bool first_bar = true;
			int first_treble_bar = 0;
			int bar_buffer[number_of_bars + 1];

			for (int n = 0; n < number_of_bars + 1; n++) {
				double bar_distribution_coefficient = frequency_constant * (-1);
				bar_distribution_coefficient +=
					((float)n + 1) / ((float)number_of_bars + 1) * frequency_constant;
				cut_off_frequency[n] = p.upper_cut_off * pow(10, bar_distribution_coefficient);

				if (n > 0) {
					if (cut_off_frequency[n - 1] >= cut_off_frequency[n] &&
							cut_off_frequency[n - 1] > bass_cut_off)
						cut_off_frequency[n] =
							cut_off_frequency[n - 1] +
							(cut_off_frequency[n - 1] - cut_off_frequency[n - 2]);
				}

				relative_cut_off[n] = cut_off_frequency[n] / (audio.rate / 2);
				// remember nyquist!, per my calculations this should be rate/2
				// and nyquist freq in M/2 but testing shows it is not...
				// or maybe the nq freq is in M/4

				eq[n] = pow(cut_off_frequency[n], 1);

				// the numbers that come out of the FFT are verry high
				// the EQ is used to "normalize" them by dividing with this verry huge number
				eq[n] *= (float)height / pow(2, 28);

				if (p.userEQ_enabled)
					eq[n] *= p.userEQ[(int)floor(((double)n) * userEQ_keys_to_bars_ratio)];

				eq[n] /= log2(audio.FFTbassbufferSize);

				if (cut_off_frequency[n] < bass_cut_off) {
					// BASS
					bar_buffer[n] = 1;
					FFTbuffer_lower_cut_off[n] =
						relative_cut_off[n] * (audio.FFTbassbufferSize / 2);
					bass_cut_off_bar++;
					treble_cut_off_bar++;
					if (bass_cut_off_bar > 0)
						first_bar = false;

					eq[n] *= log2(audio.FFTbassbufferSize);
				} else if (cut_off_frequency[n] > bass_cut_off &&
						cut_off_frequency[n] < treble_cut_off) {
					// MID
					bar_buffer[n] = 2;
					FFTbuffer_lower_cut_off[n] = relative_cut_off[n] * (audio.FFTmidbufferSize / 2);
					treble_cut_off_bar++;
					if ((treble_cut_off_bar - bass_cut_off_bar) == 1) {
						first_bar = true;
						FFTbuffer_upper_cut_off[n - 1] =
							relative_cut_off[n] * (audio.FFTbassbufferSize / 2);
					} else {
						first_bar = false;
					}

					eq[n] *= log2(audio.FFTmidbufferSize);
				} else {
					// TREBLE
					bar_buffer[n] = 3;
					FFTbuffer_lower_cut_off[n] =
						relative_cut_off[n] * (audio.FFTtreblebufferSize / 2);
					first_treble_bar++;
					if (first_treble_bar == 1) {
						first_bar = true;
						FFTbuffer_upper_cut_off[n - 1] =
							relative_cut_off[n] * (audio.FFTmidbufferSize / 2);
					} else {
						first_bar = false;
					}

					eq[n] *= log2(audio.FFTtreblebufferSize);
				}

				if (n > 0) {
					if (!first_bar) {
						FFTbuffer_upper_cut_off[n - 1] = FFTbuffer_lower_cut_off[n] - 1;

						// pushing the spectrum up if the exponential function gets "clumped" in the
						// bass and caluclating new cut off frequencies
						if (FFTbuffer_lower_cut_off[n] <= FFTbuffer_lower_cut_off[n - 1]) {

							FFTbuffer_lower_cut_off[n] = FFTbuffer_lower_cut_off[n - 1] + 1;
							FFTbuffer_upper_cut_off[n - 1] = FFTbuffer_lower_cut_off[n] - 1;

							if (bar_buffer[n] == 1)
								relative_cut_off[n] = (float)(FFTbuffer_lower_cut_off[n]) /
									((float)audio.FFTbassbufferSize / 2);
							else if (bar_buffer[n] == 2)
								relative_cut_off[n] = (float)(FFTbuffer_lower_cut_off[n]) /
									((float)audio.FFTmidbufferSize / 2);
							else if (bar_buffer[n] == 3)
								relative_cut_off[n] = (float)(FFTbuffer_lower_cut_off[n]) /
									((float)audio.FFTtreblebufferSize / 2);

							cut_off_frequency[n] = relative_cut_off[n] * ((float)audio.rate / 2);
						}
					} else {
						if (FFTbuffer_upper_cut_off[n - 1] <= FFTbuffer_lower_cut_off[n - 1])
							FFTbuffer_upper_cut_off[n - 1] = FFTbuffer_lower_cut_off[n - 1] + 1;
					}
					upper_cut_off_frequency[n - 1] =
						cut_off_frequency[n]; // high_relative_cut_off * ((float)audio.rate / 2);
					center_frequencies[n - 1] =
						pow((cut_off_frequency[n - 1] * upper_cut_off_frequency[n - 1]), 0.5);
				}
			}

			if (p.stereo)
				number_of_bars = number_of_bars * 2;

			// process: calculate x axis values
			int x_axis_info = 0;

			if (p.xaxis != NONE) {
				x_axis_info = 1;
				double center_frequency;

				for (int n = 0; n < number_of_bars; n++) {
					if (p.stereo) {
						if (n < number_of_bars / 2)
							center_frequency = center_frequencies[number_of_bars / 2 - 1 - n];
						else
							center_frequency = center_frequencies[n - number_of_bars / 2];
					} else {
						center_frequency = center_frequencies[n];
					}

					float freq_kilohz = center_frequency / 1000;
					int freq_floor = center_frequency;
				}
			}

			bool resizeTerminal = false;

			int frame_time_msec = (1 / (float)p.framerate) * 1000;
			struct timespec framerate_timer = {.tv_sec = 0, .tv_nsec = 0};
			if (p.framerate <= 1) {
				framerate_timer.tv_sec = frame_time_msec * 1000;
			} else {
				framerate_timer.tv_sec = 0;
				framerate_timer.tv_nsec = frame_time_msec * 1e6;
			}

			int sleep_counter = 0;
			bool silence = false;
			char ch = '\0';

#ifndef NDEBUG
			int maxvalue = 0;
			int minvalue = 0;
#endif

			struct timespec sleep_mode_timer = {.tv_sec = 1, .tv_nsec = 0};

			int total_frames = 0;

			while (!resizeTerminal) {
				if (should_reload) {

					reloadConf = true;
					resizeTerminal = true;
					should_reload = 0;
				}

				if (reload_colors) {
					struct error_s error;
					error.length = 0;
					if (!load_config(configPath, (void *)&p, 1, &error)) {
						cleanup();
						fprintf(stderr, "Error loading config. %s", error.message);
						exit(EXIT_FAILURE);
					}
					resizeTerminal = true;
					reload_colors = 0;
				}

#ifndef NDEBUG
				// clear();
				refresh();
#endif

				// process: check if input is present
				silence = true;

				for (int n = 0; n < audio.FFTbassbufferSize; n++) {
					if (audio.in_bass_l[n] || audio.in_bass_r[n]) {
						silence = false;
						break;
					}
				}

				if (output_mode != OUTPUT_SDL) {
					if (p.sleep_timer) {
						if (silence && sleep_counter <= p.framerate * p.sleep_timer)
							sleep_counter++;
						else if (!silence)
							sleep_counter = 0;

						if (sleep_counter > p.framerate * p.sleep_timer) {
#ifndef NDEBUG
							printw("no sound detected for 30 sec, going to sleep mode\n");
#endif
							nanosleep(&sleep_mode_timer, NULL);
							continue;
						}
					}
				}

				// process: execute FFT and sort frequency bands
				pthread_mutex_lock(&lock);
				fftw_execute(p_bass_l);
				fftw_execute(p_mid_l);
				fftw_execute(p_treble_l);
				if (p.stereo) {
					fftw_execute(p_bass_r);
					fftw_execute(p_mid_r);
					fftw_execute(p_treble_r);
					number_of_bars /= 2;
				}
				pthread_mutex_unlock(&lock);

				// process: separate frequency bands
				for (int n = 0; n < number_of_bars; n++) {

					temp_l[n] = 0;
					if (p.stereo)
						temp_r[n] = 0;

					// process: add upp FFT values within bands
					for (int i = FFTbuffer_lower_cut_off[n]; i <= FFTbuffer_upper_cut_off[n]; i++) {
						if (n <= bass_cut_off_bar) {

							temp_l[n] += hypot(out_bass_l[i][0], out_bass_l[i][1]);

							if (p.stereo)
								temp_r[n] += hypot(out_bass_r[i][0], out_bass_r[i][1]);

						} else if (n > bass_cut_off_bar && n <= treble_cut_off_bar) {

							temp_l[n] += hypot(out_mid_l[i][0], out_mid_l[i][1]);

							if (p.stereo)
								temp_r[n] += hypot(out_mid_r[i][0], out_mid_r[i][1]);

						} else if (n > treble_cut_off_bar) {

							temp_l[n] += hypot(out_treble_l[i][0], out_treble_l[i][1]);

							if (p.stereo)
								temp_r[n] += hypot(out_treble_r[i][0], out_treble_r[i][1]);
						}
					}

					// getting average multiply with sens and eq
					temp_l[n] /= FFTbuffer_upper_cut_off[n] - FFTbuffer_lower_cut_off[n] + 1;
					temp_l[n] *= p.sens * eq[n];

					if (temp_l[n] <= p.ignore)
						temp_l[n] = 0;

					bars_left[n] = temp_l[n];

					if (p.stereo) {
						temp_r[n] /= FFTbuffer_upper_cut_off[n] - FFTbuffer_lower_cut_off[n] + 1;
						temp_r[n] *= p.sens * eq[n];

						if (temp_r[n] <= p.ignore)
							temp_r[n] = 0;

						bars_right[n] = temp_r[n];
					}
				}
				if (p.stereo)
					number_of_bars *= 2;
				// process [filter]

				if (p.monstercat) {
					if (p.stereo) {
						bars_left =
							monstercat_filter(bars_left, number_of_bars / 2, p.waves, p.monstercat);
						bars_right = monstercat_filter(bars_right, number_of_bars / 2, p.waves,
								p.monstercat);
					} else {
						bars_left =
							monstercat_filter(bars_left, number_of_bars, p.waves, p.monstercat);
					}
				}

				// processing signal

				bool senselow = true;

				for (int n = 0; n < number_of_bars; n++) {
					// mirroring stereo channels
					if (p.stereo) {
						if (n < number_of_bars / 2) {
							if (p.reverse) {
								bars[n] = bars_left[n];
							} else {
								bars[n] = bars_left[number_of_bars / 2 - n - 1];
							}
						} else {
							if (p.reverse) {
								bars[n] = bars_right[number_of_bars - n - 1];
							} else {
								bars[n] = bars_right[n - number_of_bars / 2];
							}
						}

					} else {
						if (p.reverse) {
							bars[n] = bars_left[number_of_bars - 1 - n];
						} else {
							bars[n] = bars_left[n];
						}
					}

					// process [smoothing]: falloff
					if (g > 0) {
						if (bars[n] < bars_last[n]) {
							bars[n] = bars_peak[n] - (g * fall[n] * fall[n]);
							if (bars[n] < 0)
								bars[n] = 0;
							fall[n]++;
						} else {
							bars_peak[n] = bars[n];
							fall[n] = 0;
						}

						bars_last[n] = bars[n];
					}

					// process [smoothing]: integral
					if (p.integral > 0) {
						bars[n] = bars_mem[n] * integral + bars[n];
						bars_mem[n] = bars[n];

						int diff = height - bars[n];
						if (diff < 0)
							diff = 0;
						double div = 1 / (diff + 1);
						// bars[n] = bars[n] - pow(div, 10) * (height + 1);
						bars_mem[n] = bars_mem[n] * (1 - div / 20);
					}
#ifndef NDEBUG
					mvprintw(n, 0, "%d: f:%f->%f (%d->%d), eq:\
							%15e, peak:%d \n",
							n, cut_off_frequency[n], cut_off_frequency[n + 1],
							FFTbuffer_lower_cut_off[n], FFTbuffer_upper_cut_off[n], eq[n],
							bars[n]);

					if (bars[n] < minvalue) {
						minvalue = bars[n];
						debug("min value: %d\n", minvalue); // checking maxvalue 10000
					}
					if (bars[n] > maxvalue) {
						maxvalue = bars[n];
					}
					if (bars[n] < 0) {
						debug("negative bar value!! %d\n", bars[n]);
						//    exit(EXIT_FAILURE); // Can't happen.
					}

#endif

					// zero values causes divided by zero segfault (if not raw)
					if (output_mode != OUTPUT_RAW && bars[n] < 1)
						bars[n] = 1;

					// automatic sense adjustment
					if (p.autosens && !silence) {
						if (bars[n] > height && senselow) {
							p.sens = p.sens * 0.98;
							senselow = false;
							first = false;
						}
					}
				}

				if (p.autosens && !silence && senselow) {
					p.sens = p.sens * 1.001;
					if (first)
						p.sens = p.sens * 1.1;
				}

#ifndef NDEBUG
				mvprintw(number_of_bars + 1, 0, "sensitivity %.10e", p.sens);
				mvprintw(number_of_bars + 2, 0, "min value: %d\n",
						minvalue); // checking maxvalue 10000
				mvprintw(number_of_bars + 3, 0, "max value: %d\n",
						maxvalue); // checking maxvalue 10000
				(void)x_axis_info;
#endif

				// output: draw processed input
				int rc;

				// terminal has been resized breaking to recalibrating values
				if (rc == -1)
					resizeTerminal = true;

				if (rc == -2) {
					resizeTerminal = true;
					reloadConf = true;
					should_quit = true;
				}

				memcpy(previous_frame, bars, MAX_BARS * sizeof(int));

				// checking if audio thread has exited unexpectedly
				if (audio.terminate == 1) {
					cleanup();
					fprintf(stderr, "Audio thread exited unexpectedly. %s\n", audio.error_message);
					exit(EXIT_FAILURE);
				}

				if (output_mode != OUTPUT_SDL)
					nanosleep(&framerate_timer, NULL);

				if (p.draw_and_quit > 0) {
					total_frames++;
					if (total_frames >= p.draw_and_quit) {
						for (int n = 0; n < number_of_bars; n++) {
							if (output_mode != OUTPUT_RAW && bars[n] == 1) {
								bars[n] = 0;
							}
							total_bar_height += bars[n];
						}
						resizeTerminal = true;
						reloadConf = true;
						should_quit = true;
						break;
					}
				}

				// Copy to display bars
				display->num_bars = number_of_bars;
				for (int n = 0; n < number_of_bars; n++)
					display->bars[n + 1].value = ((float) bars[n])/height;

				// Render to the display
				render(display);
			} // resize terminal

		} // reloading config

		//**telling audio thread to terminate**//
		audio.terminate = 1;
		pthread_join(p_thread, NULL);

		if (p.userEQ_enabled)
			free(p.userEQ);

		free(audio.source);

		fftw_free(audio.in_bass_r);
		fftw_free(audio.in_bass_l);
		fftw_free(out_bass_r);
		fftw_free(out_bass_l);
		fftw_destroy_plan(p_bass_l);
		fftw_destroy_plan(p_bass_r);

		fftw_free(audio.in_mid_r);
		fftw_free(audio.in_mid_l);
		fftw_free(out_mid_r);
		fftw_free(out_mid_l);
		fftw_destroy_plan(p_mid_l);
		fftw_destroy_plan(p_mid_r);

		fftw_free(audio.in_treble_r);
		fftw_free(audio.in_treble_l);
		fftw_free(out_treble_r);
		fftw_free(out_treble_l);
		fftw_destroy_plan(p_treble_l);
		fftw_destroy_plan(p_treble_r);

		cleanup();

		if (should_quit) {
			if (p.zero_test && total_bar_height > 0) {
				fprintf(stderr, "Test mode: expected total bar height to be zero, but was: %d\n",
						total_bar_height);
				return EXIT_FAILURE;
			} else if (p.non_zero_test && total_bar_height == 0) {
				fprintf(stderr,
						"Test mode: expected total bar height to be non-zero, but was zero\n");
				return EXIT_FAILURE;
			} else {
				return EXIT_SUCCESS;
			}
		}
		// fclose(fp);
	}
}
