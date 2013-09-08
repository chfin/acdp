# acdp

A minimalistic ALSA Audio-CD player.

acdp plays single tracks from a CD over ALSA. It can be controlled via stdin.
It can be used as a minimalistic commandline cd player but is also inteded to be used for scripting or to be called from other programms.

## Usage

```
$ acdp
```

plays track 1 from the standard CD drive to "default".

```
$ acdp -i /dev/cdrom -o "default" 3
```

plays track 3, input and output devices are specified.
Here is the full list of options:

    -h          print this help
    -c level    cdparanoia correction, 0 disabled (default), 1 full without neverskip, 2 full
    -i device   input device, if not given, libcdio selects a device (usually /dev/cdrom)
    -o device   alsa devive, defaults to "default"
    -s speed    drive speed, defaults to 2 which means slowest speed for most devices (and is most silent)
  -r          human readable status output (old format), instead of JSON
  -q	      print CDDB info in JSON and exit (respects -i)

## Commands

During playback you can use the following commands to control playback:

    pause    pauses playback
    resume   resumes playback when paused
    stop     stops and exits
    seek o   jumps o sectors relative to the current sector
    jump n   jumps to sector n
    status   prints the current playback status (playing/paused, track, current sector, total sectors)

Note, that `seek` and `jump` do not resume paused playback.
The output of `status` is notated in JSON, so it can be easily parsed by another program.
If you the old, more human readable format, use the `-r` option.
Information about the player state ("playing"/"paused"), the track (the you started the player with), the current sector, and the track's total number of sectors.
For example:

    {"status": "paused", "track": 1, "sector": 409, "length": 96627}

You can calculate the current/total time in seconds from sectors by dividing by 75, since 75 sectors of audio data make up 1 second.
So in the above example the playback is at 0:05 (409/74 = 5, rounding down) and the total time is 21:28 (96627/75 = 1288 = 21*60 + 28, rounding down).
Will always want to round the current time down, if you want second accuracy, since the displayed time switches when a second is completed.

## CDDB information

Running `acdp -q` uses lib cddb to query freedb.org for metadata.
The output is similar to this:

    {
      "firsttrack": 1,
      "trackcount": 4,
      "seconds": 4675,
      "discid": 756171012,
      "title": "Mahler:Symphony N.6-Disk 08",
      "artist": "Leonard Bernstein & New York Philharmonic",
      "genre": "Classical",
      "year": 2012,
      "tracks": [
        {
          "number": 1,
          "length": 1288,
          "title": "Symphony No. 6 in A minor \"Tragic\" - I. Allegro energico, ma non troppo. Heftig, aber markig",
          "artist": "Leonard Bernstein & New York Philharmonic"
        },
        {
          "number": 2,
          "length": 744,
          "title": "II. Scherzo. Wuchtig",
          "artist": "Leonard Bernstein & New York Philharmonic"
        },
        {
          "number": 3,
          "length": 919,
          "title": "III. Andante moderato",
          "artist": "Leonard Bernstein & New York Philharmonic"
        },
        {
          "number": 4,
          "length": 1720,
          "title": "IV. Finale. Allegro moderato - Allegro energico",
          "artist": "Leonard Bernstein & New York Philharmonic"
        }
      ]
    }

## Dependencies

* libalsa
* libcdio
* libcddb

Note, that cdio might be split into a seperate package like `libcdio-paranoia` for its version of cdparanoia (acdp needs both).

## Building

Just run `build.sh`.
