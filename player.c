#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <alsa/asoundlib.h>
#include <cdio/cdio.h>
#include <cdio/paranoia/cdda.h>
#include <cdio/paranoia/paranoia.h>
#include <cdio/cd_types.h>

typedef enum {
  CMD_NONE,
  CMD_SEEK,
  CMD_JUMP,
  CMD_PAUSE,
  CMD_RESUME,
  CMD_STOP
} command_t;

//description of the current playback state
char* pb_state = "stopped";

/* prints a help message */
void usage (char* command) {
  printf ("usage %s [OPTION]... [TRACK]\n\
\n\
Options:\n\
  -h          print this help\n\
  -c level    cdparanoia correction, 0 disabled (default), 1 full without neverskip, 2 full\n\
  -i device   input device, if not given, libcdio selects a device (usually /dev/cdrom)\n\
  -o device   alsa devive, defaults to \"default\"\n\
  -s speed    drive speed, defaults to 2 which means slowest speed for most devices (and is most silent)\n\
\n\
The TRACK argument specifies the track to play, by default 1.\n\
\n\
Examples:\n\
%s\n\
    plays track 1 from /dev/cdrom to \"default\" on most systems\n\
%s -i /dev/cdrom -o default 1\n\
    does the same with explicitly specified options\n", command, command, command);
  exit (0);
}

/* exits with error message, if err<0 */
void ensure (int err, char* msg) {
  if (err < 0) {
    fprintf (stderr, "Error: %s (%s)", msg, snd_strerror (err));
    exit(1);
  }
}

/* sets alsa device to 44.1k 16bit stereo interleaved */
void setup (snd_pcm_t *playback_handle) {
  snd_pcm_hw_params_t *hw_params;

  //allocate hw_params
  ensure (snd_pcm_hw_params_malloc (&hw_params), "cannot allocate hw_params");

  //init hw_params
  ensure (snd_pcm_hw_params_any (playback_handle, hw_params), "cannot init hw_params");

  //set access mode interleaved
  ensure (snd_pcm_hw_params_set_access (playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED),
	  "cannot set access mode");

  //set format
  ensure (snd_pcm_hw_params_set_format (playback_handle, hw_params, SND_PCM_FORMAT_S16_LE),
	  "cannot set format");

  //set rate
  ensure (snd_pcm_hw_params_set_rate (playback_handle, hw_params, 44100, 0),
	  "cannot set sample rate");

  //set stereo
  ensure (snd_pcm_hw_params_set_channels (playback_handle, hw_params, 2), "cannot set stereo");
  
  //set all params to device
  ensure (snd_pcm_hw_params (playback_handle, hw_params), "cannot set parameters");
  snd_pcm_hw_params_free (hw_params);
}

/* checks, if a new line is ready at stdin */
/* from http://cc.byexamples.com/2007/04/08/non-blocking-user-input-in-loop-without-ncurses/ */
int kbhit()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
    select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

/* prints a status message */
void print_status (track_t track, lsn_t first, lsn_t last, lsn_t lsn) {
  printf("Status: %s, track %i, time: %i of %i\n", pb_state, track, lsn-first, last-first);
}

/* parses commands on stdin */
command_t process_commands (track_t track, lsn_t first, lsn_t last, lsn_t lsn, int *seek) {
  //check for new command line
  if (!kbhit()) {
    return CMD_NONE;
  }

  //read line from stdin
  char line[32];
  if (!fgets(line, sizeof (line), stdin)) {
    return CMD_NONE;
  }

  //parse line
  if (strncmp ("pause", line, 5) == 0) {
    pb_state = "paused";
    printf ("Paused.\n");
    return CMD_PAUSE;
  } else if (strncmp ("resume", line, 6) == 0) {
    pb_state = "playing";
    printf ("Resumed.\n");
    return CMD_RESUME;
  } else if (strncmp ("stop", line, 4) == 0)
    return CMD_STOP;
  else if (strncmp ("status", line, 6) == 0) {
    print_status (track, first, last, lsn);
    return CMD_NONE;
  } else if (sscanf (line, "seek %d", seek) == 1)
    return CMD_SEEK;
  else if (sscanf (line, "jump %d", seek) == 1)
    return CMD_JUMP;
  else
    return CMD_NONE;
}

/* seeks cd relative to current position within track boundaries, returns new position (sector) */
lsn_t seek_relative (int32_t offset, lsn_t lsn, lsn_t first, lsn_t last, cdrom_paranoia_t *p) {
  lsn_t pos = lsn+offset;

  printf ("Seeking relative to from %i to ", lsn);

  if (pos < first) {
    printf ("%i.\n", first);
    cdio_paranoia_seek (p, first, SEEK_SET);
    return first;
  } else if (pos > last) {
    printf ("%i.\n", last);
    cdio_paranoia_seek (p, last, SEEK_SET);
    return last;
  } else {
    printf ("%i.\n", pos);
    cdio_paranoia_seek (p, offset, SEEK_CUR);
    return pos;
  }
}

/* seeks cd, jumps to absolute position within track boundaries, returns new position (sector) */
lsn_t seek_absolute (int32_t pos, lsn_t lsn, lsn_t first, lsn_t last, cdrom_paranoia_t *p) {
  if (pos < first) pos = first;
  else if (pos > last) pos = last;
  
  printf ("Jumping from %i to %i.\n", lsn, pos);
  
  cdio_paranoia_seek (p, pos, SEEK_SET);
}

/* opens devices and plays a track, listening for commands on stdin */
void play (CdIo_t* cdio_drive, track_t track, char* alsa_device, int mode, int speed) {
  int i;
  int err;
  snd_pcm_t *playback_handle;
  cdrom_drive_t *drive;
  cdrom_paranoia_t *p;
  int16_t *readbuf;
  const short n = CD_FRAMEWORDS/2;
  int stopped = 0;
  int paused = 0;

  //open cdrom drive
  drive = cdio_cddap_identify_cdio (cdio_drive, 1, NULL);
  cdio_cddap_open (drive);
  cdio_cddap_verbose_set(drive, CDDA_MESSAGE_FORGETIT, CDDA_MESSAGE_FORGETIT);
  cdio_cddap_speed_set (drive, speed);
  p = cdio_paranoia_init (drive);
  //  paranoia_modeset(p, PARANOIA_MODE_FULL^PARANOIA_MODE_NEVERSKIP);
  paranoia_modeset(p, mode);

  //get track boundaries and seek to track start
  lsn_t first_lsn = cdio_cddap_track_firstsector (drive, track);
  lsn_t   last_lsn = cdio_cddap_track_lastsector(drive, track);
  cdio_paranoia_seek(p, first_lsn, SEEK_SET);

  //open alsa device
  ensure (snd_pcm_open (&playback_handle, alsa_device, SND_PCM_STREAM_PLAYBACK, 0),
	  "cannot open alsa device");
  setup (playback_handle);  //setup 44.1k 16bit stereo interleaved
  ensure (snd_pcm_prepare (playback_handle), "cannot prepare device");

  pb_state = "playing";

  //read and output audio data
  for (i=first_lsn+1; i<=last_lsn && !stopped;) {
    //read and play, except when paused
    if (!paused) {
      readbuf = cdio_paranoia_read (p, NULL);
      if ((err = snd_pcm_writei (playback_handle, readbuf, n)) != n) {
	//fprintf (stderr, "write to audio interface failed (%s), frame %i\n", snd_strerror (err), i);
	snd_pcm_prepare (playback_handle);
	snd_pcm_writei (playback_handle, readbuf, n); //write again
      }
      i++;
    }
    
    //process commands
    int seek;
    switch (process_commands (track, first_lsn, last_lsn, i, &seek)) {
    case CMD_NONE: break;
    case CMD_PAUSE: paused = 1; break;
    case CMD_RESUME: paused = 0; break;
    case CMD_STOP: stopped = 1; break;
    case CMD_SEEK: i = seek_relative (seek, i, first_lsn, last_lsn, p); break;
    case CMD_JUMP: i = seek_absolute (seek, i, first_lsn, last_lsn, p); break;
    }
  }

  pb_state = "stopped";

  //tell if stopped or finished
  if (stopped) {
    printf ("Stopped.\n");
  } else {
    printf ("Done.\n");
  }

  //close alsa device
  snd_pcm_close (playback_handle);

  //close cdrom drive
  cdio_paranoia_free (p);
  cdio_cddap_close (drive);
}

int main (int argc, char *argv[]) {
  char *drive = NULL;
  char *alsa_device = "default";
  int c;
  int track = 1;
  int mode, clevel = 0;
  int speed = 2;

  //parse options
  while ((c = getopt (argc, argv, "i:o:c:s:h")) != -1) {
    switch (c) {
    case 'i': drive = optarg; break;
    case 'o': alsa_device = optarg; break;
    case 'c': sscanf (optarg, "%d", &clevel); break;
    case 's': sscanf (optarg, "%d", &speed); break;
    case 'h': usage(argv[0]); break;
    }
  }

  //set paranoia mode (0=disable, 1=full/neverskip, 2=full)
  switch (clevel) {
  case 1: mode = PARANOIA_MODE_FULL^PARANOIA_MODE_NEVERSKIP; break;
  case 2: mode = PARANOIA_MODE_FULL; break;
  default: mode = PARANOIA_MODE_DISABLE;
  }

  //parse track argument
  if (optind < argc) {
    if (sscanf(argv[optind], "%d", &track) != 1) {
      fprintf (stderr, "Error: cannot parse track argument (%s)!\n", argv[optind]);
      exit (1);
    }
  }

  //open cd device
  CdIo_t *cdio = cdio_open (drive, DRIVER_DEVICE);
  if (cdio == NULL)
    return;

  //check track range
  track_t first_track = cdio_get_first_track_num (cdio);
  track_t last_track = first_track+cdio_get_num_tracks (cdio)-1;
  if (track < first_track || track > last_track) {
    fprintf (stderr, "Error: track number out of range (%i - %i)!\n", first_track, last_track);
    exit (1);
  }

  //print decisions/options
  printf ("Using drive \"%s\".\n", cdio_get_default_device(cdio));
  printf ("Using alsa device \"%s\".\n", alsa_device);
  printf ("Correction mode is %i.\n", mode);
  printf ("Attempting drive speed %i.\n", speed);
  printf ("Playing track %i.\n", track);
  
  //play the track
  play (cdio, track, alsa_device, mode, speed);
  return 0;
}
