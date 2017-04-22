//
//  renderer.h
//  C-Ray
//
//  Created by Valtteri Koskivuori on 19/02/2017.
//  Copyright © 2017 Valtteri Koskivuori. All rights reserved.
//

#ifndef renderer_h
#define renderer_h

#include "includes.h"
#include "vector.h"
#include "color.h"
#include "scene.h"
#include "poly.h"

typedef struct {
#ifdef WINDOWS
	HANDLE thread_handle;
	DWORD thread_id;
#else
	pthread_t thread_id;
#endif
	int thread_num;
	bool threadComplete;
}threadInfo;

typedef struct {
	int width;
	int height;
	int startX, startY;
	int endX, endY;
	int completedSamples;
	bool isRendering;
	int tileNum;
}renderTile;

typedef struct {
	threadInfo *renderThreadInfo;
#ifndef WINDOWS
	pthread_attr_t renderThreadAttributes;
#endif
	scene *worldScene;
	renderTile *renderTiles;
	int tileCount;
	int renderedTileCount;
	double *renderBuffer;
	unsigned char *uiBuffer;
	int threadCount;
	int activeThreads;
	bool shouldSave;
	bool isRendering;
	bool renderAborted;
}renderer;

//Renderer
extern renderer mainRenderer;
#ifdef WINDOWS
	DWORD WINAPI renderThread(LPVOID arg);
#else
	void *renderThread(void *arg);
#endif
void quantizeImage(scene *worldScene);
void reorderTiles(renderOrder order);

#endif /* renderer_h */
