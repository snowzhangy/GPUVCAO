/**********************************************************************

 **********************************************************************/

#ifndef __APPLYVC__H
#define __APPLYVC__H

#include "Max.h"
#include "notify.h"
#include "resource.h"
#include "utilapi.h"
#include "istdplug.h"

#include "GPUAO_kernel.cuh"

//classID defined in istdplug.h
#define GPUVCAO_UTIL_CLASS_ID	Class_ID(0x24b5411b, 0x47dd62ab)

class ModInfo {
public:
	Modifier*	mod;
	INodeTab	nodes;
};

typedef IVertexPaint::VertColorTab	ColorTab;
typedef IAssignVertexColors_R7::Options2   VCAOOptions;

class MeshInstance {
public:

	MeshInstance(INode* node);
	~MeshInstance();

	INode*	node;
	Mesh*	mesh;	// A copy of the mesh

	BOOL	negParity;

	Point3	center;	// Bounding sphere
	float	radsq;
	TriObject* GetTriObjectFromNode(INode *node, TimeValue t, int &deleteIt);
	BOOL TMNegParity(Matrix3 &m);
	Box3	boundingBox;
	//BOOL	bTwoSidedMaterial;
};
typedef Tab<MeshInstance*> MeshInstanceTab;

class GPUVCAOUtil : public UtilityObj
{
public:
	IUtil		*iu;
	Interface	*ip;
	HWND		hPanel;
	MeshInstanceTab	meshNodes;

	//UI
	ICustEdit*			iMapChanEdit;
	ISpinnerControl*	iMapChanSpin;
	ICustEdit*			iPassesEdit;
	ISpinnerControl*	iPassesSpin;
	ICustEdit*			iDistanceEdit;
	ISpinnerControl*	iDistanceSpin;
	int			mapChannelSpin; //the current value in the map channel spinner
	VCAOOptions currentOptions;

	static void NotifyRefreshUI(void* param, NotifyInfo* info);

	GPUVCAOUtil();
	~GPUVCAOUtil();

	void	BeginEditParams(Interface *ip,IUtil *iu);
	void	EndEditParams(Interface *ip,IUtil *iu);
	void	DeleteThis() {}

	void	Init(HWND hWnd);
	void	Destroy(HWND hWnd);

	void	PrePareHost();
	void	DeviceWork();
	void	CPUWork();
	int		ApplySelected();
	void	_finalize();
	void	UpdateUI();
	void    UpdateEnableStates();
	void	SaveOptions();
	void	LoadOptions();
	void	GetOptions( IAssignVertexColors::Options& options );
	void	SetOptions( IAssignVertexColors::Options& options );
	void	EditColors();

	Modifier*	GetModifier(INode* node, Class_ID modCID);
	void	AddModifier(Tab<INode*>& nodes, Modifier* mod);
	void	DeleteModifier( INode* nodes, Modifier* mod );
	BOOL	GetChannelName( int index, TSTR& name );
	
	AOParams m_params;

protected:
	//cpu data
	float*	m_vhPos;
	uint*  m_vhFaces;
	float* m_vhNor;
	float* m_vhArea;
	float*  m_vhColors;
	//gpu data
	float*	m_vdPos;
	uint*	m_vdFaces;
	float*  m_vdNor;
	float*  m_vdArea;
	float*  m_vdColors;
	///////
	uint m_timer;

	void getNormalArea();

	Modifier* recentMod;
	void	InitMemory(uint totalVerts,uint totalFaces);
	void	InitGPU(uint totalVerts,uint totalFaces);
	void	FinalizeGPU();
};
extern ClassDesc*	GetGPUVCAOUtilDesc();
extern HINSTANCE	hInstance;
extern TCHAR*		GetString(int id);

#endif // __APPLYVC__H
