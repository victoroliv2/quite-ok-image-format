#include <inttypes.h>
#include <stdio.h>
#include <byteswap.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

struct image
{
	uint8_t *data;
	uint32_t width;
	uint32_t height;
	uint8_t channels;
};

void read_ppm(const char * filename, struct image *img)
{
	FILE *f = fopen(filename, "r");
	fscanf(f, "P6 %d %d 255\n", &img->width, &img->height);
	img->channels = 3;
	img->data = malloc(img->width * img->height * img->channels);
	fread(&img->data[0], sizeof(uint8_t), img->width * img->height * img->channels, f);
	fclose(f);
}

void write_ppm(const struct image *img, const char * filename)
{
	FILE *f = fopen(filename, "w");
	fprintf(f, "P6 %d %d 255\n", img->width, img->height);
	uint8_t * data = img->data;
	for (int i = 0; i < img->width * img->height; i++) {
		fwrite(&data[0], sizeof(uint8_t), 3, f);
		data += img->channels;
	}
	fclose(f);
}

struct qoi_header
{
	char magic[4]; 		// magic bytes "qoif"
	uint32_t width; 	// image width in pixels (BE)
	uint32_t height; 	// image height in pixels (BE)
	uint8_t channels; 	// 3 = RGB, 4 = RGBA
	uint8_t colorspace; // 0 = sRGB with linear alpha
						// 1 = all channels linear
};
// struct has gaps/is padded!
// static_assert(sizeof(struct qoi_header) == 11);

struct pixel4
{
	uint8_t r, g, b, a;
};

static struct pixel4 lookup[64];

_Bool pixel4Equal(struct pixel4 a,struct pixel4 b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

uint32_t index_position(struct pixel4 p)
{
	return (p.r * 3u + p.g * 5u + p.b * 7u + p.a * 11u) % 64u;
}

union QOI_OP
{
	uint8_t u8;
	struct
   	{
   		unsigned int value : 6;
   		unsigned int op : 2;
	};
};
// fun fact: sizeof(QOI_OP) == sizeof(unsigned int) == 4

union QOI_OP_DIFF
{
	uint8_t u8;
	struct
   	{
   		unsigned int db : 2;
   		unsigned int dg : 2;
   		unsigned int dr : 2;
   		unsigned int op : 2;
	};
};

union QOI_OP_LUMA_2
{
	uint8_t u8;
	struct
   	{
   		unsigned int db_dg : 4;
   		unsigned int dr_dg : 4;
	};
};

void encode_qok(struct image * img, FILE * f)
{
	memset(&lookup[0], 0, sizeof(lookup));

	struct qoi_header header = {
		.magic      = {'q','o','i','f'},
        .width      = __bswap_32(img->width),
		.height     = __bswap_32(img->height),
		.channels   = 3,
		.colorspace = 0
   	};

	fwrite(&header.magic,      sizeof(header.magic),      1, f);
	fwrite(&header.width,      sizeof(header.width),      1, f);
	fwrite(&header.height,     sizeof(header.height),     1, f);
	fwrite(&header.channels,   sizeof(header.channels),   1, f);
	fwrite(&header.colorspace, sizeof(header.colorspace), 1, f);

	uint32_t datasize = img->width * img->height * header.channels;

	uint32_t index = 0;
	struct pixel4 p = { 0, 0, 0, 255 };

	uint8_t * raw_image = img->data;

	for (;;)
   	{
		struct pixel4 c = { 0, 0, 0, 255 };

		int32_t run = 0;
		for(; index < datasize; run++)
	   	{
			c.r = raw_image[index++];
			c.g = raw_image[index++];
			c.b = raw_image[index++];
			if (header.channels == 4) { c.a = raw_image[index++]; };
			if (!pixel4Equal(p,c)) break;
		}

		for (; run > 0; run -= 62)
		{
			union QOI_OP qoiOp = {
				.op = 0b11,
				.value = (run < 62 ? run : 62) - 1
			};
			fwrite(&qoiOp.u8, sizeof(uint8_t), 1, f);
		}

		if (index >= datasize) break;

		int32_t lookup_index = index_position(c);

		int32_t dr = (int32_t)c.r - (int32_t)p.r;
		int32_t dg = (int32_t)c.g - (int32_t)p.g;
		int32_t db = (int32_t)c.b - (int32_t)p.b;

		int32_t dr_dg = dr - dg;
		int32_t db_dg = db - dg;

		if (pixel4Equal(lookup[lookup_index], c))
		{ // QOI_OP_INDEX
			union QOI_OP qoiOp = {
			   	.op = 0b00,
			   	.value = lookup_index
		   	};
			fwrite(&qoiOp.u8, sizeof(uint8_t), 1, f);
		}
		else if ( // QOI_OP_DIFF
			c.a == p.a &&
			dr >= -2 && dr < +2 &&
			dg >= -2 && dg < +2 &&
			db >= -2 && db < +2)
		{
			union QOI_OP_DIFF qoiOpDiff = {
			   	.op = 0b01,
			   	.dr = dr+2,
			   	.dg = dg+2,
			   	.db = db+2
		   	};
			fwrite(&qoiOpDiff.u8, sizeof(uint8_t), 1, f);
		}
		else if ( // QOI_OP_LUMA
			c.a == p.a &&
			dg >= -32 && dg < +32 &&
			dr_dg >= -8 && dr_dg < +8 &&
			db_dg >= -8 && db_dg < +8)
		{
			union QOI_OP qoiOp = {
				.op = 0b10,
				.value = dg+32
		   	};
			fwrite(&qoiOp.u8, sizeof(uint8_t), 1, f);

			union QOI_OP_LUMA_2 qoiOp2 = {
			   	.dr_dg = dr_dg+8,
			   	.db_dg = db_dg+8
		   	};
			fwrite(&qoiOp2.u8, sizeof(uint8_t), 1, f);
		}
		else if (header.channels == 4)
		{ 
			// only channel = 3 supported
			assert(0);
		}
		else {
			fwrite(&(uint8_t){254}, sizeof(uint8_t), 1, f);
			fwrite(&c.r, sizeof(uint8_t), 1, f);
			fwrite(&c.g, sizeof(uint8_t), 1, f);
			fwrite(&c.b, sizeof(uint8_t), 1, f);
		}

		lookup[index_position(c)] = c;
		p = c;
	}

	uint8_t end_of_file[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };
	fwrite(&end_of_file[0], sizeof(uint8_t), 8, f);
}


void decode_qok(FILE * f, struct image * img) {
	memset(&lookup[0], 0, sizeof(lookup));

	struct qoi_header header = { 0 };
	fread(&header.magic,      sizeof(header.magic),      1, f);
	fread(&header.width,      sizeof(header.width),      1, f);
	fread(&header.height,     sizeof(header.height),     1, f);
	fread(&header.channels,   sizeof(header.channels),   1, f);
	fread(&header.colorspace, sizeof(header.colorspace), 1, f);

	header.width  = __bswap_32 (header.width);
	header.height = __bswap_32 (header.height);

	uint32_t npixels = header.width * header.height;
	uint8_t * raw_image = malloc(npixels * header.channels);

	assert(strncmp(header.magic, "qoif", 4) == 0 && "invalid file type");

	uint32_t index = 0;
	struct pixel4 p = { 0, 0, 0, 255 };
	while (index < npixels * header.channels)
   	{
		uint8_t byte1;
		fread(&byte1, sizeof(uint8_t), 1, f);

		uint32_t run = 1;

		switch (byte1)
	   	{
		case 254: // QOI_OP_RGB
			fread(&p, sizeof(uint8_t), 3, f);
			break;
		case 255: // QOI_OP_RGBA
			fread(&p, sizeof(uint8_t), 4, f);
			break;
		default:
			uint32_t op1 = (byte1 >> 6);

			switch (op1)
		   	{
			case 0: // QOI_OP_INDEX
			{
				union QOI_OP qoiOp = { .u8 = byte1 };
				assert(qoiOp.op == 0);
				p = lookup[qoiOp.value];
				break;
			}
			case 1: // QOI_OP_DIFF
			{
				union QOI_OP_DIFF qoiOpDiff = { .u8 = byte1 };
				p.r = (int32_t)p.r + (int32_t)qoiOpDiff.dr - 2;
				p.g = (int32_t)p.g + (int32_t)qoiOpDiff.dg - 2;
				p.b = (int32_t)p.b + (int32_t)qoiOpDiff.db - 2;
				break;
			}
			case 2: // QOI_OP_LUMA
			{
				union QOI_OP qoiOp1 = { .u8 = byte1 };
				union QOI_OP_LUMA_2 qoiOp2 = { .u8 = 0 };
				fread(&qoiOp2.u8, sizeof(uint8_t), 1, f);

				int32_t dg = (int32_t)qoiOp1.value - 32;
				int32_t dr = (int32_t)qoiOp2.dr_dg - 8 + dg;
				int32_t db = (int32_t)qoiOp2.db_dg - 8 + dg;

				p.r = (int32_t)p.r + dr;
				p.g = (int32_t)p.g + dg;
				p.b = (int32_t)p.b + db;
				break;
			}
			case 3: // QOI_OP_RUN
			{	
				union QOI_OP qoiOp = { .u8 = byte1 };
				run = qoiOp.value + 1;
				assert(qoiOp.value < 63);
				break;
			}
			default:
				assert(0 && "invalid chunk");
			}
		}
					
		for (int32_t j = 0; j < run; j++)
	   	{
			switch (header.channels)
		   	{
			case 3:
				raw_image[index++] = p.r;
				raw_image[index++] = p.g;
				raw_image[index++] = p.b;
				break;

			case 4:
				raw_image[index++] = p.r;
				raw_image[index++] = p.g;
				raw_image[index++] = p.b;
				raw_image[index++] = p.a;
				break;
			}
		}
	
		lookup[index_position(p)] = p;
	}

	{
		uint8_t end_of_file[8];
		fread(&end_of_file[0], sizeof(uint8_t), 8, f);
		uint8_t match[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };

		for (int j = 0; j < 8; j++)
			assert(end_of_file[j] == match[j]);
	}

	img->data     = raw_image;
	img->width    = header.width;
	img->height   = header.height;
	img->channels = header.channels;
}

int main(int argc, const char * argv[]) {
	assert(argc == 4 && "wrong cmg args");

	if (strcmp(argv[1], "decode") == 0)
	{
		FILE * f = fopen(argv[2], "rb");
		struct image img = {};
		decode_qok(f, &img);
		fclose(f);
		write_ppm(&img, argv[3]);
		free(img.data);
	}
	else if (strcmp(argv[1], "encode") == 0)
	{
		struct image img = {};
		read_ppm(argv[2], &img);
		FILE * f = fopen(argv[3], "wb");
		encode_qok(&img, f);
		fclose(f);
		free(img.data);
	}
	else
	{
		assert(0 && "invalid option");
	}

	return 0;
}
