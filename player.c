#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <alsa/asoundlib.h>
#include <cdio/cdio.h>
#include <cdio/cdtext.h>
#include <cdio/paranoia/cdda.h>
#include <cdio/paranoia/paranoia.h>
#include <cdio/cd_types.h>
#include <cddb/cddb.h>
#include <utf8proc.h>

typedef enum {
  CMD_NONE,
  CMD_SEEK,
  CMD_JUMP,
  CMD_PAUSE,
  CMD_RESUME,
  CMD_STOP
} command_t;

char* pb_state = "stopped"; //description of the current playback state
int json_p = 1; //boolean, 1 if status message should be JSON, 0 if human readable

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
  -r          human readable status output (old format), instead of JSON\n\
  -q          print CDDB info in JSON and exit (respects -i)\n\
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

/* escape quotation marks in strings */
char* escape_qm (const char* str) {
  int i, lold, lnew, len, qcnt = 0;
  len = strlen (str);

  //count " in the string
  for (i=0; i<len; i++) {
    if (str[i] == '"') qcnt++;
  }

  //allocate a buffer for the string plus a \ per " (plus null-character)
  char *res = (char*) malloc (len+qcnt+1);

  //copy string in blocks between "s and insert \s
  lold = 0; lnew = 0;
  for (i=0; i<len; i++) {
    if (str[i] == '"') {
      int n = i-lold;
      strncpy (res+lnew, str+lold, n);
      lold = i;
      lnew += n;
      res[lnew] = '\\';
      lnew++;
    }
  }
  strncpy (res+lnew, str+lold, len-lold); //copy the remaining part

  char* fixed;
  utf8proc_map (res, 0, (uint8_t**) &fixed, UTF8PROC_NULLTERM);
  free (res);

  return fixed;
}

/* output cd information */
void print_info (char* drive) {
  int i;

  //open cd drive
  CdIo_t *cdio = cdio_open (drive, DRIVER_DEVICE);
  if (cdio == NULL)
    return;
  
  //get info about tracks
  track_t first_track = cdio_get_first_track_num (cdio);
  track_t track_count = cdio_get_num_tracks (cdio);
  track_t last_track = first_track + track_count - 1;

  //create a disc template for a cddb query
  cddb_disc_t *disc = cddb_disc_new ();

  //get total length in seconds
  int length = cdio_get_track_lba(cdio, CDIO_CDROM_LEADOUT_TRACK)
    / CDIO_CD_FRAMES_PER_SEC;
  cddb_disc_set_length (disc, length);

  //get the tracks' offsets
  cddb_track_t *track;
  for (i=first_track; i<=last_track; i++) {
    track = cddb_track_new ();
    cddb_track_set_frame_offset (track, cdio_get_track_lba (cdio, i));
    cddb_disc_add_track (disc, track);
  }

  //new cddb connection
  cddb_conn_t *conn = NULL;
  conn = cddb_new ();
  cddb_set_charset (conn, "UTF-8");
  
  //query the cddb
  int matches = cddb_query(conn, disc);
  //printf ("Matches: %i\n", matches);
  cddb_read (conn, disc);
  //cddb_disc_print (disc);

  //print the collected info as JSON and escape " in strings
  char* title = escape_qm (cddb_disc_get_title (disc));
  char* artist = escape_qm (cddb_disc_get_artist (disc));
  char* genre = escape_qm (cddb_disc_get_genre (disc));
  printf ("{\n\
  \"firsttrack\": %i,\n\
  \"trackcount\": %i,\n\
  \"seconds\": %i,\n\
  \"discid\": %i,\n\
  \"title\": \"%s\",\n\
  \"artist\": \"%s\",\n\
  \"genre\": \"%s\",\n\
  \"year\": %i,\n\
  \"tracks\": [\n",
	  first_track, track_count, length,
	  cddb_disc_get_discid (disc), title,
	  artist, genre, cddb_disc_get_year (disc));
  fflush(stdout);
  free ((void*) title);
  free ((void*) artist);
  free ((void*) genre);

  //print per-track info
  for (i=0; i<track_count; i++) {
    track = cddb_disc_get_track (disc, i);
    title = escape_qm(cddb_track_get_title (track));
    artist = escape_qm(cddb_track_get_artist (track));
    
    printf ("    {\n\
      \"number\": %i,\n\
      \"length\": %i,\n\
      \"title\": \"%s\",\n\
      \"artist\": \"%s\"\n\
    }%s\n",
	    cddb_track_get_number (track), cddb_track_get_length (track),
	    title, artist, (i==track_count-1)?"":",");
    free ((void*) title);
    free ((void*) artist);
  }

  printf ("  ]\n}\n");

  fflush (stdout);
  cdio_destroy (cdio);
  cddb_disc_destroy (disc);
  cddb_destroy (conn);
}

/* exits with error message, if err<0 */
void ensure (int err, char* msg) {
  if (err < 0) {
    fprintf (stderr, "Error: %s (%s)", msg, snd_strerror (err));
    exit(-1);
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
  if (json_p) {
    printf("{\"status\": \"%s\", \"track\": %i, \"sector\": %i, \"length\": %i}\n",
	   pb_state, track, lsn-first, last-first);
  } else {
    printf("Status: %s, track %i, time: %i of %i\n",
	   pb_state, track, lsn-first, last-first);
  }
  fflush(stdout);
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
    fflush(stdout);
    return CMD_PAUSE;
  } else if (strncmp ("resume", line, 6) == 0) {
    pb_state = "playing";
    printf ("Resumed.\n");
    fflush(stdout);
    return CMD_RESUME;
  } else if (strncmp ("stop", line, 4) == 0)
    return CMD_STOP;
  else if (strncmp ("status", line, 6) == 0) {
    print_status (track, first, last, lsn);
    return CMD_NONE;
    //} else if (strncmp ("info", line, 4) == 0) {
    //print_info ();
    //return CMD_NONE;
  } else if (sscanf (line, "seek %d", seek) == 1)
    return CMD_SEEK;
  else if (sscanf (line, "jump %d", seek) == 1)
    return CMD_JUMP;
  else {
    printf ("Unknown comman: %s", line);
    fflush(stdout);
    return CMD_NONE;
  }
}

/* seeks cd relative to current position within track boundaries, returns new position (sector) */
lsn_t seek_relative (int32_t offset, lsn_t lsn, lsn_t first, lsn_t last, cdrom_paranoia_t *p) {
  lsn_t pos = lsn+offset;

  printf ("Seeking relative to from %i to ", lsn);

  if (pos < first) {
    printf ("%i.\n", first);
    fflush(stdout);
    cdio_paranoia_seek (p, first, SEEK_SET);
    return first;
  } else if (pos > last) {
    printf ("%i.\n", last);
    fflush(stdout);
    cdio_paranoia_seek (p, last, SEEK_SET);
    return last;
  } else {
    printf ("%i.\n", pos);
    fflush(stdout);
    cdio_paranoia_seek (p, offset, SEEK_CUR);
    return pos;
  }
}

/* seeks cd, jumps to absolute position within track boundaries, returns new position (sector) */
lsn_t seek_absolute (int32_t pos, lsn_t lsn, lsn_t first, lsn_t last, cdrom_paranoia_t *p) {
  pos += first; // absolute within the track, not the disc!
  if (pos < first) pos = first;
  else if (pos > last) pos = last;
  
  printf ("Jumping from %i to %i.\n", lsn, pos);
  fflush(stdout);
  
  cdio_paranoia_seek (p, pos, SEEK_SET);

  return pos;
}

/* opens devices and plays a track, listening for commands on stdin */
int play (CdIo_t* cdio_drive, track_t track,
	  char* alsa_device, int mode, int speed) {
  int i;
  int err;
  snd_pcm_t *playback_handle;
  snd_pcm_sframes_t delay, old_delay;
  cdrom_drive_t *drive;
  cdrom_paranoia_t *p;
  int16_t *readbuf;
  int stopped = 0;
  int paused = 0;

  const short N = CD_FRAMEWORDS/2;
  const short MAX_DELAY = N*4;

  //open cdrom drive
  drive = cdio_cddap_identify_cdio (cdio_drive, 1, NULL);
  cdio_cddap_open (drive);
  cdio_cddap_verbose_set(drive, CDDA_MESSAGE_FORGETIT, CDDA_MESSAGE_FORGETIT);
  cdio_cddap_speed_set (drive, speed);
  p = cdio_paranoia_init (drive);
  //  paranoia_modeset(p, PARANOIA_MODE_FULL^PARANOIA_MODE_NEVERSKIP);
  paranoia_modeset(p, mode);
  fflush(stdout);

  //get track boundaries and seek to track start
  lsn_t first_lsn = cdio_cddap_track_firstsector (drive, track);
  lsn_t last_lsn = cdio_cddap_track_lastsector(drive, track);
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
      if ((err = snd_pcm_writei (playback_handle, readbuf, N)) != N) {
	//fprintf (stderr, "write to audio interface failed (%s), frame %i\n", snd_strerror (err), i);
	snd_pcm_prepare (playback_handle);
	snd_pcm_writei (playback_handle, readbuf, N); //write again
      }
      
      //wait for playback
      snd_pcm_delay (playback_handle, &delay);
      snd_pcm_avail_update (playback_handle);
      old_delay = 0;
      while (delay > MAX_DELAY && delay != old_delay) {
	usleep (10);
	old_delay = delay;
	snd_pcm_delay (playback_handle, &delay); 
	snd_pcm_avail_update (playback_handle);
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

  //close alsa device
  snd_pcm_close (playback_handle);

  //close cdrom drive
  cdio_paranoia_free (p);
  cdio_cddap_close (drive);

  //tell if stopped or finished
  if (stopped) {
    printf ("Stopped.\n");
    return 1;
  } else {
    printf ("Done.\n");
    return 0;
  }
}

int main (int argc, char *argv[]) {
  char *drive = NULL;
  char *alsa_device = "default";
  int c;
  int track = 1;
  int mode, clevel = 0;
  int speed = 2;
  int query = 0; // boolean, -q option flag

  //parse options
  while ((c = getopt (argc, argv, "i:o:c:s:rqh")) != -1) {
    switch (c) {
    case 'i': drive = optarg; break;
    case 'o': alsa_device = optarg; break;
    case 'c': sscanf (optarg, "%d", &clevel); break;
    case 's': sscanf (optarg, "%d", &speed); break;
    case 'r': json_p = 0; break;
    case 'q': query = 1; break;
    case 'h': usage(argv[0]); break;
    }
  }

  if (query == 1) {
    print_info (drive);
    exit(0);
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
      exit (-1);
    }
  }

  //open cd device
  CdIo_t *cdio = cdio_open (drive, DRIVER_DEVICE);
  if (cdio == NULL)
    return -1;

  //check track range
  track_t first_track = cdio_get_first_track_num (cdio);
  track_t last_track = first_track+cdio_get_num_tracks (cdio)-1;
  if (track < first_track || track > last_track) {
    fprintf (stderr, "Error: track number out of range (%i - %i)!\n", first_track, last_track);
    exit (-1);
  }

  //print decisions/options
  printf ("Using drive \"%s\".\n", cdio_get_default_device(cdio));
  printf ("Using alsa device \"%s\".\n", alsa_device);
  printf ("Correction mode is %i.\n", mode);
  printf ("Attempting drive speed %i.\n", speed);
  printf ("Playing track %i.\n", track);
  fflush (stdout);

  //play the track
  return play (cdio, track, alsa_device, mode, speed);
}
