#!/bin/sh

gcc -lasound -lcdio -lcdio_cdda -lcdio_paranoia -lcddb player.c -o acdp
