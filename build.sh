#!/bin/sh

gcc -lasound -lcdio -lcdio_cdda -lcdio_paranoia player.c -o acdp
