//
//  filehandler.c
//  C-ray
//
//  Created by Valtteri Koskivuori on 28/02/2015.
//  Copyright © 2015-2020 Valtteri Koskivuori. All rights reserved.
//

#include "../libraries/asprintf.h"
#include "../includes.h"
#include "filehandler.h"

#include "../datatypes/camera.h"
#include "../datatypes/scene.h"
#include "../renderer/renderer.h"
#include "../utils/logging.h"
#include "../datatypes/texture.h"

#include "../libraries/lodepng.h"
#include "assert.h"

#include <limits.h> //For SSIZE_MAX

#ifndef WINDOWS
#include <sys/utsname.h>
#include <libgen.h>
#endif

//Prototypes for internal functions
size_t getFileSize(char *fileName);
size_t getDelim(char **lineptr, size_t *n, int delimiter, FILE *stream);

void encodeBMPFromArray(const char *filename, unsigned char *imgData, int width, int height) {
	//Apparently BMP is BGR, whereas C-ray's internal buffer is RGB (Like it should be)
	//So we need to convert the image data before writing to file.
	unsigned char *bgrData = calloc(3 * width * height, sizeof(unsigned char));
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			bgrData[(x + (height - (y + 1)) * width) * 3 + 0] = imgData[(x + (height - (y + 1)) * width) * 3 + 2];
			bgrData[(x + (height - (y + 1)) * width) * 3 + 1] = imgData[(x + (height - (y + 1)) * width) * 3 + 1];
			bgrData[(x + (height - (y + 1)) * width) * 3 + 2] = imgData[(x + (height - (y + 1)) * width) * 3 + 0];
		}
	}
	int i;
	int error;
	FILE *f;
	int filesize = 54 + 3 * width * height;
	unsigned char bmpfileheader[14] = {'B','M', 0,0,0,0, 0,0, 0,0, 54,0,0,0};
	unsigned char bmpinfoheader[40] = {40,0,0,0, 0,0,0,0, 0,0,0,0, 1,0, 24,0};
	unsigned char bmppadding[3] = {0,0,0};
	//Create header with filesize data
	bmpfileheader[2] = (unsigned char)(filesize    );
	bmpfileheader[3] = (unsigned char)(filesize>> 8);
	bmpfileheader[4] = (unsigned char)(filesize>>16);
	bmpfileheader[5] = (unsigned char)(filesize>>24);
	//create header width and height info
	bmpinfoheader[ 4] = (unsigned char)(width    );
	bmpinfoheader[ 5] = (unsigned char)(width>>8 );
	bmpinfoheader[ 6] = (unsigned char)(width>>16);
	bmpinfoheader[ 7] = (unsigned char)(width>>24);
	bmpinfoheader[ 8] = (unsigned char)(height    );
	bmpinfoheader[ 9] = (unsigned char)(height>>8 );
	bmpinfoheader[10] = (unsigned char)(height>>16);
	bmpinfoheader[11] = (unsigned char)(height>>24);
	f = fopen(filename,"wb");
	error = (unsigned int)fwrite(bmpfileheader,1,14,f);
	if (error != 14) {
		logr(warning, "Error writing BMP file header data\n");
	}
	error = (unsigned int)fwrite(bmpinfoheader,1,40,f);
	if (error != 40) {
		logr(warning, "Error writing BMP info header data\n");
	}
	for (i = 1; i <= height; ++i) {
		error = (unsigned int)fwrite(bgrData+(width*(height - i)*3),3,width,f);
		if (error != width) {
			logr(warning, "Error writing image line to BMP\n");
		}
		error = (unsigned int)fwrite(bmppadding,1,(4-(width*3)%4)%4,f);
		if (error != (4-(width*3)%4)%4) {
			logr(warning, "Error writing BMP padding data\n");
		}
	}
	free(bgrData);
	fclose(f);
}

void encodePNGFromArray(const char *filename, unsigned char *imgData, int width, int height, struct renderInfo imginfo) {
	LodePNGInfo info;
	lodepng_info_init(&info);
	info.time_defined = 1;
	
	char version[60];
	sprintf(version, "C-ray v%s [%.8s], © 2015-2020 Valtteri Koskivuori", imginfo.crayVersion, imginfo.gitHash);
	char samples[16];
	sprintf(samples, "%i", imginfo.samples);
	char bounces[16];
	sprintf(bounces, "%i", imginfo.bounces);
	char renderTime[64];
	smartTime(imginfo.renderTime, renderTime);
	char threads[16];
	sprintf(threads, "%i", imginfo.threadCount);
#ifndef WINDOWS
	char sysinfo[1300];
	struct utsname name;
	uname(&name);
	sprintf(sysinfo, "%s %s %s %s %s", name.machine, name.nodename, name.release, name.sysname, name.version);
#endif
	
	lodepng_add_text(&info, "C-ray Version", version);
	lodepng_add_text(&info, "C-ray Source", "https://github.com/vkoskiv/c-ray");
	lodepng_add_text(&info, "C-ray Samples", samples);
	lodepng_add_text(&info, "C-ray Bounces", bounces);
	lodepng_add_text(&info, "C-ray RenderTime", renderTime);
	lodepng_add_text(&info, "C-ray Threads", threads);
#ifndef WINDOWS
	lodepng_add_text(&info, "C-ray SysInfo", sysinfo);
#endif
	
	LodePNGState state;
	lodepng_state_init(&state);
	state.info_raw.bitdepth = 8;
	state.info_raw.colortype = LCT_RGB;
	lodepng_info_copy(&state.info_png, &info);
	state.encoder.add_id = 1;
	state.encoder.text_compression = 0;
	
	size_t bytes = 0;
	unsigned char *buf = NULL;
	
	unsigned error = lodepng_encode(&buf, &bytes, imgData, width, height, &state);
	if (error) logr(warning, "Error %u: %s\n", error, lodepng_error_text(error));
	
	error = lodepng_save_file(buf, bytes, filename);
	if (error) logr(warning, "Error %u: %s\n", error, lodepng_error_text(error));
	
	lodepng_info_cleanup(&info);
	lodepng_state_cleanup(&state);
	free(buf);
}

//TODO: Use this for textures and HDRs too.
char *loadFile(char *inputFileName, size_t *bytes) {
	FILE *f = fopen(inputFileName, "rb");
	if (!f) {
		logr(warning, "No file found at %s\n", inputFileName);
		return NULL;
	}
	char *buf = NULL;
	size_t len;
	size_t bytesRead = getDelim(&buf, &len, '\0', f);
	if (bytesRead > 0) {
		if (bytes) *bytes = bytesRead;
	} else {
		logr(warning, "Failed to read input file from %s", inputFileName);
		fclose(f);
		return NULL;
	}
	fclose(f);
	return buf;
}

//Wait for 2 secs and abort if nothing is coming in from stdin
void checkBuf() {
#ifndef WINDOWS
	fd_set set;
	struct timeval timeout;
	int rv;
	FD_ZERO(&set);
	FD_SET(0, &set);
	timeout.tv_sec = 2;
	timeout.tv_usec = 1000;
	rv = select(1, &set, NULL, NULL, &timeout);
	if (rv == -1) {
		logr(error, "Error on stdin timeout\n");
	} else if (rv == 0) {
		logr(error, "No input found after %i seconds. Hint: Try `./bin/c-ray input/scene.json`.\n", timeout.tv_sec);
	} else {
		return;
	}
#endif
}


//TODO: Make these consistent. Now I have to free getFilePath, but not getFileName
/**
 Extract the filename from a given file path

 @param input File path to be processed
 @return Filename string, including file type extension
 */
char *getFileName(char *input) {
	char *fn;
	
	/* handle trailing '/' e.g.
	 input == "/home/me/myprogram/" */
	if (input[(strlen(input) - 1)] == '/')
		input[(strlen(input) - 1)] = '\0';
	
	(fn = strrchr(input, '/')) ? ++fn : (fn = input);
	
	return fn;
}

//For Windows
#define CRAY_PATH_MAX 4096

char *getFilePath(char *input) {
	char *dir = NULL;
#ifdef WINDOWS
	dir = calloc(256, sizeof(char));
	_splitpath_s(input, NULL, 0, dir, sizeof(dir), NULL, 0, NULL, 0);
#else
	copyString(dirname(input), &dir);
#endif
	return concatString(dir, "/");
}

#define chunksize 1024
//Get scene data from stdin and return a pointer to it
char *readStdin(size_t *bytes) {
	checkBuf();
	
	char chunk[chunksize];
	
	size_t bufSize = 1;
	char *buf = malloc(chunksize * sizeof(char));
	if (!buf) {
		logr(error, "Failed to malloc stdin buffer\n");
		return NULL;
	}
	buf[0] = '\0';
	while (fgets(chunk, chunksize, stdin)) {
		char *old = buf;
		bufSize += strlen(chunk);
		buf = realloc(buf, bufSize);
		if (!buf) {
			logr(error, "Failed to realloc stdin buffer\n");
			free(old);
			return NULL;
		}
		strcat(buf, chunk);
	}
	
	if (ferror(stdin)) {
		logr(error, "Failed to read from stdin\n");
		free(buf);
		return NULL;
	}
	
	if (bytes) *bytes = bufSize - 1;
	return buf;
}

//FIXME: Move this to a better place
bool stringEquals(const char *s1, const char *s2) {
	if (strcmp(s1, s2) == 0) {
		return true;
	} else {
		return false;
	}
}
//FIXME: Move this to a better place
bool stringContains(const char *haystack, const char *needle) {
	if (strstr(haystack, needle) == NULL) {
		return false;
	} else {
		return true;
	}
}

char *humanFileSize(unsigned long bytes) {
	unsigned long kilobytes, megabytes, gigabytes, terabytes; // <- Futureproofing?!
	kilobytes = bytes / 1000;
	megabytes = kilobytes / 1000;
	gigabytes = megabytes / 1000;
	terabytes = gigabytes / 1000;
	
	char *buf = calloc(64, sizeof(char));
	
	if (gigabytes > 1000) {
		sprintf(buf, "%ldTB", terabytes);
	} else if (megabytes > 1000) {
		sprintf(buf, "%ldGB", gigabytes);
	} else if (kilobytes > 1000) {
		sprintf(buf, "%ldMB", megabytes);
	} else if (bytes > 1000) {
		sprintf(buf, "%ldKB", kilobytes);
	} else {
		sprintf(buf, "%ldB", bytes);
	}
	return buf;
}

void printFileSize(char *fileName) {
	//We determine the file size after saving, because the lodePNG library doesn't have a way to tell the compressed file size
	//This will work for all image formats
	unsigned long bytes = getFileSize(fileName);
	char *sizeString = humanFileSize(bytes);
	logr(info, "Wrote %s to file.\n", sizeString);
	free(sizeString);
}

void writeImage(struct texture *image, struct renderInfo imginfo) {
	//Save image data to a file
	char *buf = NULL;
	if (image->fileType == bmp){
		asprintf(&buf, "%s%s_%04d.bmp", image->filePath, image->fileName, image->count);
		encodeBMPFromArray(buf, image->byte_data, image->width, image->height);
	} else if (image->fileType == png){
		asprintf(&buf, "%s%s_%04d.png", image->filePath, image->fileName, image->count);
		encodePNGFromArray(buf, image->byte_data, image->width, image->height, imginfo);
	}
	logr(info, "Saving result in \"%s\"\n", buf);
	printFileSize(buf);
	free(buf);
	
}

//For Windows support, we need our own getdelim()
#if defined(_WIN32) || defined(__linux__)
#ifndef LONG_MAX
#define	LONG_MAX	2147483647L	/* max signed long */
#endif
#endif
#define	SSIZE_MAX	LONG_MAX	/* max value for a ssize_t */
size_t getDelim(char **lineptr, size_t *n, int delimiter, FILE *stream) {
	char *buf, *pos;
	int c;
	size_t bytes;
	
	if (lineptr == NULL || n == NULL) {
		return 0;
	}
	if (stream == NULL) {
		return 0;
	}
	
	/* resize (or allocate) the line buffer if necessary */
	buf = *lineptr;
	if (buf == NULL || *n < 4) {
		buf = (char*)realloc(*lineptr, 128);
		if (buf == NULL) {
			/* ENOMEM */
			return 0;
		}
		*n = 128;
		*lineptr = buf;
	}
	
	/* read characters until delimiter is found, end of file is reached, or an
	 error occurs. */
	bytes = 0;
	pos = buf;
	while ((c = getc(stream)) != -1) {
		if (bytes + 1 >= SSIZE_MAX) {
			return 0;
		}
		bytes++;
		if (bytes >= *n - 1) {
			buf = realloc(*lineptr, *n + 128);
			if (buf == NULL) {
				/* ENOMEM */
				return 0;
			}
			*n += 128;
			pos = buf + bytes - 1;
			*lineptr = buf;
		}
		
		*pos++ = (char) c;
		if (c == delimiter) {
			break;
		}
	}
	
	if (ferror(stream) || (feof(stream) && (bytes == 0))) {
		/* EOF, or an error from getc(). */
		return 0;
	}
	
	*pos = '\0';
	return bytes;
}

//Copies source over to the destination pointer.
//TODO: Move this to a better time
void copyString(const char *source, char **destination) {
	*destination = malloc(strlen(source) + 1);
	strcpy(*destination, source);
}

char *concatString(const char *str1, const char *str2) {
	ASSERT(str1); ASSERT(str2);
	char *new = malloc(strlen(str1) + strlen(str2) + 1);
	strcpy(new, str1);
	strcat(new, str2);
	return new;
}

size_t getFileSize(char *fileName) {
	FILE *file = fopen(fileName, "r");
	if (!file) return 0;
	fseek(file, 0L, SEEK_END);
	size_t size = ftell(file);
	fclose(file);
	return size;
}
