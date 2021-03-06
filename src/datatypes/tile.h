//
//  tile.h
//  C-ray
//
//  Created by Valtteri Koskivuori on 06/07/2018.
//  Copyright © 2015-2020 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include "vector.h"

struct renderer;

/**
 Render tile, contains needed information for the renderer
 */
struct renderTile {
	unsigned width;
	unsigned height;
	struct intCoord begin;
	struct intCoord end;
	int completedSamples;
	bool isRendering;
	bool renderComplete;
	int tileNum;
};

/// Quantize the render plane into an array of tiles, with properties as specified in the parameters below
/// @param renderTiles Array to place renderTiles into
/// @param width Render plane width
/// @param height Render plane height
/// @param tileWidth Tile width
/// @param tileHeight Tile height
/// @param tileOrder Order for the renderer to render the tiles in
int quantizeImage(struct renderTile **renderTiles, unsigned width, unsigned height, unsigned tileWidth, unsigned tileHeight, enum renderOrder tileOrder);

struct renderTile nextTile(struct renderer *r);
