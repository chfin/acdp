#!/bin/sh

gcc -lasound -lcdio -lcdio_cdda -lcdio_paranoia -lcddb -lutf8proc player.c -o acdp
