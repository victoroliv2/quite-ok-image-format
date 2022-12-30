# quite-ok-image-format
Reference implementation of the QOI (The Quite OK Image Format for Fast, Lossless Compression) in C99

I did an implementation from scratch reading https://qoiformat.org/qoi-specification.pdf.

I was able to encode and decode the images in https://github.com/phoboslab/qoi.

Good learning experience doing this and comparing with the official implementation after the fact.

To build:
	gcc -std=c99 -Wall qok.c -o qok
