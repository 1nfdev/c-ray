//
//  light.c
//  C-Ray
//
//  Created by Valtteri Koskivuori on 11/10/15.
//  Copyright © 2015 Valtteri Koskivuori. All rights reserved.
//

#include "includes.h"
#include "light.h"

struct light newLight(struct vector pos, double radius, struct color diffuse, double intensity) {
	return (struct light){pos, radius, colorCoef(intensity, &diffuse)};
}
