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

## Commands

During playback you can use the following commands to control playback:


    pause    pauses playback
    resume   resumes playback when paused
    stop     stops and exits
    seek o   jumps o sectors relative to the current sector
    jump n   jumps to sector n
    status   prints the current playback status (playing/paused, current sector, total sectors)

Note, that seek and jump do not resume paused playback.
The output of status is meant to be human readable and easily parsable at the same time.
I'm considering optional JSON-formatted output of status in future versions.
You can calculate the current/total time in seconds from sectors by dividing by 75, since 75 sectors of audio data make up 1 second.

## Dependencies

* libalsa
* libcdio

Note, that cdio might be split into a seperate package like `libcdio-paranoia` for its version of cdparanoia (acdp needs both).

## Building

Just run `build.sh`.
