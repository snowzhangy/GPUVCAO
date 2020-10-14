#ifndef GPUAO_SHARE_H
#define GPUAO_SHARE_H
typedef unsigned int uint;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct AOParams {
	uint	TotalVerts;
	uint	TotalFaces;
	float	Distance;
	int	Passes;
};
#endif