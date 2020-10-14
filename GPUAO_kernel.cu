/*
 * Device code.
 */

#ifndef _GPUAO_KERNEL_H_
#define _GPUAO_KERNEL_H_
#include "cutil_inline.h"
#include "cutil_math.h"
#include "GPUAO_kernel.cuh"

#define BLOCKDIM 256
__constant__ AOParams params;
__global__ void
parpareMesh(float3* DPos,uint* DFaces, float3* DNormal,float* DArea)
{
	uint index = __mul24(blockIdx.x,blockDim.x) + threadIdx.x;
	if (index<=params.TotalVerts)
	{
		float3 iNormal=make_float3(0.0f, 0.0f, 0.0f);
		float PArea=0.0f;
		uint numNormals = 0;
		for (uint f = 0; f < params.TotalFaces; f++) {
			for (uint fi = 0; fi < 3; fi++) {
				if (DFaces[f*3+fi]== index) {
					float3 V0=DPos[DFaces[f*3]];
					float3 V1=DPos[DFaces[f*3+1]];
					float3 V2=DPos[DFaces[f*3+2]];
					float3 fNormal=cross((V1-V0),(V2-V1)); 
					PArea+=length(fNormal)/2.0f;
					iNormal += fNormal;
					numNormals++;
				}
			}
		}
		iNormal = normalize(iNormal);
		
		if (numNormals>0)
		{
			DNormal[index] = iNormal ;
			DArea[index]=PArea/((float)numNormals*M_PI);
		}
		else
		{
			DNormal[index] = make_float3(0.0f, 0.0f, 0.0f);
			DArea[index]=0.0f;
		}

	}
}
__global__ void
AOProcess(float3* DPos, float3* DNormal,float* DArea,float* DColor,uint pass)
{
	int index = __mul24(blockIdx.x,blockDim.x) + threadIdx.x;
	if (index<=params.TotalVerts)
	{
		__shared__ float3 posTmp;
		__shared__ float3 NormalTmp;
		float  areaTmp;
		float3 v;
		float d2;
		float value;
		float total=0.0f; 
		
		posTmp=DPos[index];
		NormalTmp=DNormal[index];
		for(int i=0;i<params.TotalVerts;i++)
		{
			areaTmp=DArea[i];
			v=DPos[i]-posTmp;
			if (length(v)>params.Distance||i==index||length(v)==0)
				continue;
			d2=dot(v,v)+1e-16;
			if(d2<-4*areaTmp)
			{
				DArea[i]=0.0f;
			}
			v*= rsqrt(d2);
			value=(1.0f -rsqrt(abs(areaTmp)/d2 + 1.0f))*saturate(dot(DNormal[i], v))*saturate(3.0f*dot(NormalTmp, v));
			if (pass==2)
				value*=DColor[i];
			NormalTmp-= value*v;
			total += value;
		}
		DNormal[index]=NormalTmp;
		if (pass==1)
			DColor[index] = saturate(1.0f-total);
		else
			DColor[index] = DColor[index]*0.4f+ saturate(1.0f - total)*0.6f;
	}
}
extern "C"
{
	void cudaInit()
	{   
		cudaSetDevice( cutGetMaxGflopsDeviceId() );
	}
	void allocateArray(void **devPtr, size_t size)
	{
		cutilSafeCall(cudaMalloc(devPtr, size));
	}
	void freeArray(void *devPtr)
	{
		cutilSafeCall(cudaFree(devPtr));
	}
	void threadSync()
	{
		cutilSafeCall(cudaThreadSynchronize());
	}
	void copyArrayFromDevice(void* host, const void* device, int size)
	{   
		cutilSafeCall(cudaMemcpy(host, device, size, cudaMemcpyDeviceToHost));
	}
	void copyArrayToDevice(void* device, const void* host, int offset, int size)
	{
		cutilSafeCall(cudaMemcpy((char *) device + offset, host, size, cudaMemcpyHostToDevice));
	}
	void setParameters(AOParams *hostParams)
	{
		// copy parameters to constant memory
		cutilSafeCall(cudaMemcpyToSymbol(params, hostParams, sizeof(AOParams)) );
	}

	int iDivUp(int a, int b){
		return (a % b != 0) ? (a / b + 1) : (a / b);
	}
	// compute grid and thread block size for a given number of elements
	void computeGridSize(int n, int blockSize, int &numBlocks, int &numThreads)
	{
		numThreads = min(blockSize, n);
		numBlocks = iDivUp(n, numThreads);
	}
	void prepareMeshSystem(float* dPos,uint* dFaces, float* dNor, float* dArea,uint numVerts)
	{
		int numThreads, numBlocks;
		computeGridSize(numVerts, BLOCKDIM , numBlocks, numThreads);
		// execute the kernel
		
		//first prepare the mesh for normal and area
		parpareMesh<<< numBlocks, numThreads >>>((float3*) dPos, dFaces,(float3*) dNor,dArea);
		cutilCheckMsg("Kernel execution failed");
		
	}
	void integrateSystem(float* dPos, float* dNor, float* dArea, float* dColor,uint numVerts,uint totalPass)
	{
		int numThreads, numBlocks;
		computeGridSize(numVerts, BLOCKDIM, numBlocks, numThreads);
		// execute the kernel
		
		//process the AO with different pass
		for(uint pass=1;pass<=totalPass;pass++)
		{
			AOProcess<<< numBlocks, numThreads >>>((float3*) dPos,(float3*) dNor,dArea, dColor,pass);
			// check if kernel invocation generated an error
			cutilCheckMsg("Kernel execution failed");
		}
	}
}
#endif