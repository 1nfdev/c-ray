//
//  main.c
//
//
//  Created by Valtteri Koskivuori on 12/02/15.
//
//

#include "includes.h"
#include "main.h"

#include "camera.h"
#include "logging.h"
#include "filehandler.h"
#include "renderer.h"
#include "scene.h"
#include "ui.h"

int getFileSize(char *fileName);
void initRenderer(struct renderer *renderer);
int getSysCores(void);
void freeMem(void);

extern struct renderer mainRenderer;
extern struct poly *polygonArray;

/**
 Main entry point

 @param argc Argument count
 @param argv Arguments
 @return Error codes, 0 if exited normally
 */
int main(int argc, char *argv[]) {
	
	time_t start, stop;
	
	//Seed RNGs
	srand((int)time(NULL));
#ifndef WINDOWS
	srand48(time(NULL));
#endif
	
	//Disable output buffering
	setbuf(stdout, NULL);
	
	//Initialize renderer
	initRenderer(&mainRenderer);
	
	char *fileName = NULL;
	//Build the scene
	if (argc == 2) {
		fileName = argv[1];
	} else {
		logr(error, "Invalid input file path.\n");
	}
	
#ifndef UI_ENABLED
	printf("**************************************************************************\n");
	printf("*      UI is DISABLED! Enable by installing SDL2 and doing `cmake .`     *\n");
	printf("**************************************************************************\n");
#endif
	
	//Build the scene
	switch (parseJSON(&mainRenderer, fileName)) {
		case -1:
			logr(error, "Scene builder failed due to previous error.");
			break;
		case 4:
			logr(warning, "Scene debug mode enabled, won't render image.");
			return 0;
			break;
		case -2:
			logr(error, "JSON parser failed.");
			break;
		default:
			break;
	}
	
	//Check and set threadCount	
	if (mainRenderer.threadCount <= 0) {
		mainRenderer.threadCount = getSysCores();
	}
	
	//Quantize image into renderTiles
	quantizeImage();
	//Reorder those tiles
	reorderTiles(mainRenderer.tileOrder);
	//Compute the focal length for the camera
	computeFocalLength(&mainRenderer);
	
#ifdef UI_ENABLED
	mainRenderer.mainDisplay->window = NULL;
	mainRenderer.mainDisplay->renderer = NULL;
	mainRenderer.mainDisplay->texture = NULL;
	mainRenderer.mainDisplay->overlayTexture = NULL;
#endif
	
	//This is a timer to elapse how long a render takes per frame
	time(&start);
	
	//Create threads
	int t;
	
	//Alloc memory for pthread_create() args
	mainRenderer.renderThreadInfo = (struct threadInfo*)calloc(mainRenderer.threadCount, sizeof(struct threadInfo));
	if (mainRenderer.renderThreadInfo == NULL) {
		logr(error, "Failed to allocate memory for threadInfo args.\n");
	}
	
	//Verify sample count
	if (mainRenderer.sampleCount < 1) {
		logr(warning, "Invalid sample count given, setting to 1\n");
		mainRenderer.sampleCount = 1;
	}
	
	logr(info, "Starting C-ray renderer for frame %i\n", mainRenderer.currentFrame);
	
	//Print a useful warning to user if the defined tile size results in less renderThreads
	if (mainRenderer.tileCount < mainRenderer.threadCount) {
		logr(warning, "WARNING: Rendering with a less than optimal thread count due to large tile size!\n");
		logr(warning, "Reducing thread count from %i to ", mainRenderer.threadCount);
		mainRenderer.threadCount = mainRenderer.tileCount;
		printf("%i\n", mainRenderer.threadCount);
	}
	
	logr(info, "Rendering at %i x %i\n", mainRenderer.image->size.width,mainRenderer.image->size.height);
	logr(info, "Rendering %i samples with %i bounces.\n", mainRenderer.sampleCount, mainRenderer.scene->bounces);
	logr(info, "Rendering with %d thread",mainRenderer.threadCount);
	if (mainRenderer.threadCount > 1) {
		printf("s.\n");
	} else {
		printf(".\n");
	}
	
	logr(info, "Pathtracing...\n");
	
	//Allocate memory and create array of pixels for image data
	mainRenderer.image->data = (unsigned char*)calloc(3 * mainRenderer.image->size.width * mainRenderer.image->size.height, sizeof(unsigned char));
	if (!mainRenderer.image->data) {
		logr(error, "Failed to allocate memory for image data.");
	}
	//Allocate memory for render buffer
	//Render buffer is used to store accurate color values for the renderers' internal use
	mainRenderer.renderBuffer = (double*)calloc(3 * mainRenderer.image->size.width * mainRenderer.image->size.height, sizeof(double));
	
	//Allocate memory for render UI buffer
	//This buffer is used for storing UI stuff like currently rendering tile highlights
	mainRenderer.uiBuffer = (unsigned char*)calloc(4 * mainRenderer.image->size.width * mainRenderer.image->size.height, sizeof(unsigned char));
	
	//Initialize SDL display
#ifdef UI_ENABLED
	initSDL();
#endif
	
	mainRenderer.isRendering = true;
	mainRenderer.renderAborted = false;
#ifndef WINDOWS
	pthread_attr_init(&mainRenderer.renderThreadAttributes);
	pthread_attr_setdetachstate(&mainRenderer.renderThreadAttributes, PTHREAD_CREATE_JOINABLE);
#endif
	//Main loop (input)
	bool threadsHaveStarted = false;
	while (mainRenderer.isRendering) {
#ifdef UI_ENABLED
		getKeyboardInput();
		drawWindow();
		SDL_UpdateWindowSurface(mainRenderer.mainDisplay->window);
#endif
		
		if (!threadsHaveStarted) {
			threadsHaveStarted = true;
			//Create render threads
			for (t = 0; t < mainRenderer.threadCount; t++) {
				mainRenderer.renderThreadInfo[t].thread_num = t;
				mainRenderer.renderThreadInfo[t].threadComplete = false;
				mainRenderer.activeThreads++;
#ifdef WINDOWS
				DWORD threadId;
				mainRenderer.renderThreadInfo[t].thread_handle = CreateThread(NULL, 0, renderThread, &mainRenderer.renderThreadInfo[t], 0, &threadId);
				if (mainRenderer.renderThreadInfo[t].thread_handle == NULL) {
					logr(error, "Failed to create thread.\n");
					exit(-1);
				}
				mainRenderer.renderThreadInfo[t].thread_id = threadId;
#else
				if (pthread_create(&mainRenderer.renderThreadInfo[t].thread_id, &mainRenderer.renderThreadAttributes, renderThread, &mainRenderer.renderThreadInfo[t])) {
					logr(error, "Failed to create a thread.\n");
				}
#endif
			}
			
			mainRenderer.renderThreadInfo->threadComplete = false;
			
#ifndef WINDOWS
			if (pthread_attr_destroy(&mainRenderer.renderThreadAttributes)) {
				logr(warning, "Failed to destroy pthread.\n");
			}
#endif
		}
		
		//Wait for render threads to finish (Render finished)
		for (t = 0; t < mainRenderer.threadCount; t++) {
			if (mainRenderer.renderThreadInfo[t].threadComplete && mainRenderer.renderThreadInfo[t].thread_num != -1) {
				mainRenderer.activeThreads--;
				mainRenderer.renderThreadInfo[t].thread_num = -1;
			}
			if (mainRenderer.activeThreads == 0 || mainRenderer.renderAborted) {
				mainRenderer.isRendering = false;
			}
		}
		sleepMSec(100);
	}
	
	//Make sure render threads are finished before continuing
	for (t = 0; t < mainRenderer.threadCount; t++) {
#ifdef WINDOWS
		WaitForSingleObjectEx(mainRenderer.renderThreadInfo[t].thread_handle, INFINITE, FALSE);
#else
		if (pthread_join(mainRenderer.renderThreadInfo[t].thread_id, NULL)) {
			logr(warning, "Thread %t frozen.", t);
		}
#endif
	}
	
	time(&stop);
	printDuration(difftime(stop, start));
	
	//Write to file
	writeImage(mainRenderer.image);
	
	mainRenderer.currentFrame++;
	
	freeMem();
	
	logr(info, "Render finished, exiting.\n");
	
	return 0;
}


/**
 Free dynamically allocated memory
 */
void freeMem() {
	//Free memory
	if (mainRenderer.image->data)
		free(mainRenderer.image->data);
	if (mainRenderer.renderThreadInfo)
		free(mainRenderer.renderThreadInfo);
	if (mainRenderer.renderBuffer)
		free(mainRenderer.renderBuffer);
	if (mainRenderer.uiBuffer)
		free(mainRenderer.uiBuffer);
	if (mainRenderer.scene->lights)
		free(mainRenderer.scene->lights);
	if (mainRenderer.scene->spheres)
		free(mainRenderer.scene->spheres);
	if (mainRenderer.scene->materials)
		free(mainRenderer.scene->materials);
	if (mainRenderer.renderTiles)
		free(mainRenderer.renderTiles);
	if (mainRenderer.scene)
		free(mainRenderer.scene);
	if (vertexArray)
		free(vertexArray);
	if (normalArray)
		free(normalArray);
	if (textureArray)
		free(textureArray);
	if (polygonArray)
		free(polygonArray);
}

void initRenderer(struct renderer *renderer) {
	renderer->scene = (struct world*)calloc(1, sizeof(struct world));
	renderer->image = (struct outputImage*)calloc(1, sizeof(struct outputImage));
	renderer->renderTiles = NULL;
	renderer->tileCount = 0;
	renderer->finishedTileCount = 0;
	renderer->renderBuffer = NULL;
	renderer->uiBuffer = NULL;
	renderer->activeThreads = 0;
	renderer->isRendering = false;
	renderer->renderPaused = false;
	renderer->renderAborted = false;
	renderer->avgTileTime = (time_t)1;
	renderer->timeSampleCount = 1;
	renderer->currentFrame = 0;
	renderer->renderThreadInfo = (struct threadInfo*)calloc(1, sizeof(struct threadInfo));
	renderer->mode = saveModeNormal;
	renderer->tileOrder = renderOrderNormal;
	renderer->inputFilePath = NULL;
	renderer->threadCount = 0;
	renderer->sampleCount = 0;
	renderer->tileWidth = 0;
	renderer->tileHeight = 0;
	renderer->antialiasing = false;
}

/**
 Sleep for a given amount of milliseconds

 @param ms Milliseconds to sleep for
 */
void sleepMSec(int ms) {
#ifdef _WIN32
	Sleep(ms);
#elif __APPLE__
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000;
	nanosleep(&ts, NULL);
#elif __linux__
	usleep(ms * 1000);
#endif
}

/**
 Get amount of logical processing cores on the system

 @return Amount of logical processing cores
 */
int getSysCores() {
#ifdef __APPLE__
	int nm[2];
	size_t len = 4;
	uint32_t count;
	
	nm[0] = CTL_HW; nm[1] = HW_AVAILCPU;
	sysctl(nm, 2, &count, &len, NULL, 0);
	
	if (count < 1) {
		nm[1] = HW_NCPU;
		sysctl(nm, 2, &count, &len, NULL, 0);
		if (count < 1) {
			count = 1;
		}
	}
	return count;
#elif _WIN32
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	return sysInfo.dwNumberOfProcessors;
#elif __linux__
	return (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
}
