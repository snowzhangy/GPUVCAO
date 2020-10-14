/**********************************************************************
*<
FILE:       AVCUtil.cpp
DESCRIPTION:   Assign Ambient Occlusion on the GPU
CREATED BY:    Yang Zhang	
HISTORY:    Created 2008/12/6

  *>  
**********************************************************************/

#include "GPUVCAO.h"
#include "3dsmaxport.h"
#include "modstack.h"
#include "cutil_inline.h"
#include "GPUAO_kernel.cuh"
#include "GPUAO_share.cuh"
static GPUVCAOUtil theGPUVCAO;

#define CFGFILE      _T("GPUVCAO.CFG")
#define CFGVERSION   5

//==============================================================================
// class ApplyVCClassDesc
//==============================================================================
class ApplyVCClassDesc:public ClassDesc {
public:
    int        IsPublic() {return 1;}
    void *        Create(BOOL loading = FALSE) {return &theGPUVCAO;}
    const TCHAR * ClassName() {return GetString(IDS_AVCU_CNAME);}
    SClass_ID     SuperClassID() {return UTILITY_CLASS_ID;}
    Class_ID      ClassID() {return GPUVCAO_UTIL_CLASS_ID;}
    const TCHAR*  Category() {return _T("");}
};

static ApplyVCClassDesc GPUVCAOUtilDesc;
ClassDesc* GetGPUVCAOUtilDesc() {return &GPUVCAOUtilDesc;}

static ActionTable* FindActionTableFromName(IActionManager* in_actionMgr, LPCTSTR in_tableName)
{
   ActionTable* foundTable = NULL;
   if (in_actionMgr) {
      int nbTables = in_actionMgr->NumActionTables();
      for (int i = 0; i < nbTables; i++) {
         ActionTable* table = in_actionMgr->GetTable(i);
         if (table && _tcscmp(table->GetName(), in_tableName) == 0) {
            foundTable = table;
            break;
         }
      }
   }
   return foundTable;
}

static ActionItem* FindActionItemFromName(ActionTable* in_actionTable, LPCTSTR in_actionName)
{
   ActionItem* foundItem = NULL;
   if (in_actionTable) {
      for (int i = 0; i < in_actionTable->Count(); i++) {
         ActionItem* actionItem = (*in_actionTable)[i];
         TSTR desc;
         if (actionItem) { 
            actionItem->GetDescriptionText(desc);
            if (_tcscmp(desc, in_actionName) == 0) {
               foundItem = actionItem;
               break;
            }
         }
      }
   }
   return foundItem;
}

static INT_PTR CALLBACK ApplyVCDlgProc(
                                       HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static BOOL initialized = FALSE;   
	switch (msg) {
    case WM_INITDIALOG:
        theGPUVCAO.Init(hWnd);
        break;
    case WM_DESTROY:
        theGPUVCAO.Destroy(hWnd);
		initialized = FALSE;
        break;
        
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
		case IDC_VCUTIL_CHAN_COLOR:		theGPUVCAO.currentOptions.mapChannel = 0;	break;
		case IDC_VCUTIL_CHAN_ILLUM:		theGPUVCAO.currentOptions.mapChannel = -1;	break;
		case IDC_VCUTIL_CHAN_ALPHA:		theGPUVCAO.currentOptions.mapChannel = -2;	break;
		case IDC_VCUTIL_CHAN_MAP:
		case IDC_VCUTIL_CHAN_MAP_EDIT:
		case IDC_VCUTIL_CHAN_MAP_SPIN:	theGPUVCAO.currentOptions.mapChannel = theGPUVCAO.iMapChanSpin->GetIVal();
		break;
		case IDC_VCUTIL_PASSES_EDIT:	theGPUVCAO.m_params.Passes=theGPUVCAO.iPassesSpin->GetIVal();
		break;
		case IDC_VCUTIL_DISTANCE_EDIT:	theGPUVCAO.m_params.Distance=theGPUVCAO.iDistanceSpin->GetFVal();
		break;
		case IDC_CLOSEBUTTON:
            theGPUVCAO.iu->CloseUtility();
            break;
        case IDC_VCUTIL_APPLYGPU:
			theGPUVCAO.PrePareHost();
			theGPUVCAO.DeviceWork();
			theGPUVCAO.ApplySelected();
			break;
		case IDC_VCUTIL_APPLYCPU:
			theGPUVCAO.PrePareHost();
			theGPUVCAO.CPUWork();
			theGPUVCAO.ApplySelected();
			break;
		case IDC_VCUTIL_EDITCOLORS:
			theGPUVCAO.EditColors();
			break;
		default: // For all other commands -> Update the UI
			theGPUVCAO.UpdateUI();
			break;
      }
        break;
	case CC_SPINNER_CHANGE:
		//ignore this message if called before InitUI() is completed
		if( LOWORD(wParam)==IDC_VCUTIL_CHAN_MAP_SPIN && initialized ) {
			theGPUVCAO.currentOptions.mapChannel = theGPUVCAO.iMapChanSpin->GetIVal();
			theGPUVCAO.UpdateUI();
		}
		break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
        theGPUVCAO.ip->RollupMouseMessage(hWnd,msg,wParam,lParam); 
        break;
        
    default:
        return FALSE;
    }
    return TRUE;
}  

void GPUVCAOUtil::NotifyRefreshUI(void* param, NotifyInfo* info) {

    GPUVCAOUtil* util = static_cast<GPUVCAOUtil*>(param);
    DbgAssert(util != NULL);
    if(util != NULL) {
        util->UpdateEnableStates();
   }
}

GPUVCAOUtil::GPUVCAOUtil()
//GPUVCAOUtil constructor
{
    iu = NULL;
    ip = NULL; 
    hPanel = NULL;
	recentMod = NULL;
    currentOptions.mixVertColors = true;
	currentOptions.mapChannel = 0, mapChannelSpin = 3; //map channel spinner defaults to 3
	currentOptions.useMaps = false;
	m_params.TotalVerts=0;
	m_params.Distance=50.0f;
	m_params.Passes=3;
	cudaInit();
}

GPUVCAOUtil::~GPUVCAOUtil()
{
	//Free memory Device

}

void GPUVCAOUtil::SaveOptions()
{
    TSTR f;
    
    f = ip->GetDir(APP_PLUGCFG_DIR);
    f += _T("\\");
    f += CFGFILE;
    
    FILE* out = fopen(f, "w");
    if (out) {
        fputc(CFGVERSION, out);
        int dummy;
        dummy = currentOptions.mixVertColors;
        fprintf(out, "%d\n", dummy);
		dummy = currentOptions.useMaps;
		fprintf(out, "%d\n", dummy);
        fclose(out);
    }
}

void GPUVCAOUtil::LoadOptions()
{
    TSTR f;
    
    f = ip->GetDir(APP_PLUGCFG_DIR);
    f += _T("\\");
    f += CFGFILE;
    
    FILE* in = fopen(f, "r");
    if (in) {
        int version;
        version = fgetc(in);  // Version
        if(version == CFGVERSION) {
            int dummy;
            fscanf(in, "%d\n", &dummy);
            currentOptions.mixVertColors = (dummy != 0);
			fscanf(in, "%d\n", &dummy);
			currentOptions.useMaps = (dummy != 0);
        }
        fclose(in);
    }
}
void GPUVCAOUtil::GetOptions( IAssignVertexColors::Options& options ) 
{
	options.mapChannel = currentOptions.mapChannel;
	options.mixVertColors = currentOptions.mixVertColors;
	options.useMaps = currentOptions.useMaps;
}
void GPUVCAOUtil::SetOptions( IAssignVertexColors::Options& options ) 
{
	currentOptions.mapChannel = options.mapChannel;
	currentOptions.mixVertColors = options.mixVertColors;
	currentOptions.useMaps = options.useMaps;
	UpdateUI();
}
void GPUVCAOUtil::BeginEditParams(Interface *ip,IUtil *iu) 
{
    this->iu = iu;
    this->ip = ip;
    ip->AddRollupPage(
        hInstance,
        MAKEINTRESOURCE(IDD_VCUTIL_PANEL),
        ApplyVCDlgProc,
        GetString(IDS_AVCU_PANELTITLE),
        0);
   LoadOptions();
   UpdateUI();
   UpdateEnableStates();
}

void GPUVCAOUtil::EndEditParams(Interface *ip,IUtil *iu) 
{
    SaveOptions();
    
    this->iu = NULL;
    this->ip = NULL;
    ip->DeleteRollupPage(hPanel);
    hPanel = NULL;
}
void GPUVCAOUtil::EditColors()
{

}

void GPUVCAOUtil::Init(HWND hWnd)
{
	hPanel = hWnd;
	HWND hMapChanEdit = GetDlgItem(hWnd,IDC_VCUTIL_CHAN_MAP_EDIT);
	iMapChanEdit = GetICustEdit( hMapChanEdit );
	iMapChanSpin = GetISpinner( GetDlgItem(hWnd,IDC_VCUTIL_CHAN_MAP_SPIN) );
	iMapChanSpin->LinkToEdit( hMapChanEdit, EDITTYPE_POS_INT );
	iMapChanSpin->SetLimits( 1, 99 );
	iMapChanSpin->SetValue( mapChannelSpin, FALSE );

	HWND hPASSEdit = GetDlgItem(hWnd,IDC_VCUTIL_PASSES_EDIT);
	iPassesEdit = GetICustEdit( hPASSEdit );
	iPassesSpin=GetISpinner( GetDlgItem(hWnd,IDC_VCUTIL_PASSES_SPIN));
	iPassesSpin->LinkToEdit( hPASSEdit, EDITTYPE_POS_INT );
	iPassesSpin->SetLimits(1,40);
	iPassesSpin->SetValue(m_params.Passes,false);

	HWND hDISTEdit = GetDlgItem(hWnd,IDC_VCUTIL_DISTANCE_EDIT);
	iDistanceEdit = GetICustEdit( hDISTEdit );
	iDistanceSpin=GetISpinner( GetDlgItem(hWnd,IDC_VCUTIL_DISTANCE_SPIN));
	iDistanceSpin->LinkToEdit( hDISTEdit, EDITTYPE_POS_FLOAT );
	iDistanceSpin->SetLimits(0.0f,1000000.0f);
	iDistanceSpin->SetValue(m_params.Distance,false);

	iPassesEdit->Enable(true);
	iDistanceEdit->Enable(true);
	iPassesSpin->Enable(true);
	iDistanceSpin->Enable(true);

}

void GPUVCAOUtil::Destroy(HWND hWnd)
{
	mapChannelSpin = iMapChanSpin->GetIVal(); //save for the next time we open the UI
	ReleaseICustEdit( iMapChanEdit );
	ReleaseISpinner( iMapChanSpin );
	ReleaseICustEdit( iPassesEdit );
	ReleaseISpinner( iPassesSpin );
	ReleaseICustEdit( iDistanceEdit );
	ReleaseISpinner( iDistanceSpin );
}

void GPUVCAOUtil::UpdateUI() {
	if( hPanel==NULL ) return;
	int channelRadioButtonID;
	switch( currentOptions.mapChannel ){
		case -2:	channelRadioButtonID = IDC_VCUTIL_CHAN_ALPHA;	break;
		case -1:	channelRadioButtonID = IDC_VCUTIL_CHAN_ILLUM;	break;
		case 0:		channelRadioButtonID = IDC_VCUTIL_CHAN_COLOR;	break;
		default:	channelRadioButtonID = IDC_VCUTIL_CHAN_MAP;		break;
	}
	iMapChanEdit->Enable( currentOptions.mapChannel>0 );
	iMapChanSpin->Enable( currentOptions.mapChannel>0 );
	iPassesSpin->SetValue( m_params.Passes, FALSE );
	iDistanceSpin->SetValue( m_params.Distance, FALSE );

	int spinValue = (currentOptions.mapChannel>0? currentOptions.mapChannel : iMapChanSpin->GetIVal());
	if( currentOptions.mapChannel>0 ) iMapChanSpin->SetValue( spinValue, FALSE );
	TSTR chanName;
	if( GetChannelName( spinValue, chanName ) )
	   SetDlgItemText( hPanel, IDC_VCUTIL_CHAN_NAME, chanName.data() );
	else SetDlgItemText( hPanel, IDC_VCUTIL_CHAN_NAME, GetString(IDS_MAPNAME_NONE) );
	CheckRadioButton(hPanel, IDC_VCUTIL_CHAN_COLOR, IDC_VCUTIL_CHAN_MAP, channelRadioButtonID);

	ICustButton* editBtn = GetICustButton( GetDlgItem(hPanel,IDC_VCUTIL_EDITCOLORS) );
	editBtn->Enable( recentMod!=NULL );
	ReleaseICustButton(editBtn);
    UpdateEnableStates();
}

void GPUVCAOUtil::UpdateEnableStates() {
    
    if (hPanel != NULL) {
    }
}

//init memory locate
void GPUVCAOUtil::InitMemory(uint totalVerts,uint totalFaces)
{
	//init host memory locate
	m_vhPos=new float[totalVerts*3];
	m_vhFaces=new uint[totalFaces*3];
	m_vhColors=new float[totalVerts];
	m_vhNor=new float[totalVerts*3];
	m_vhArea=new float[totalVerts];

	memset(m_vhPos, 0, totalVerts*3*sizeof(float));
	memset(m_vhFaces, 0, totalFaces*3*sizeof(uint));
	memset(m_vhColors, 0, totalVerts*sizeof(float));
	memset(m_vhNor, 0, totalVerts*3*sizeof(float));
	memset(m_vhArea, 0, totalVerts*sizeof(float));
}
void GPUVCAOUtil::InitGPU(uint totalVerts,uint totalFaces)
{
	//init gpu memory
	///init the device
	m_vdPos=m_vdNor=m_vdArea=m_vdColors=0;
	m_vdFaces=0;
	UINT memSizeVerts= ((totalVerts+255)&~255)*sizeof(float);
	UINT memSizeFaces= totalFaces*sizeof(uint);
	allocateArray((void**)&m_vdPos, memSizeVerts*3);
	allocateArray((void**)&m_vdFaces, memSizeFaces*3);
	allocateArray((void**)&m_vdNor, memSizeVerts*3);
	allocateArray((void**)&m_vdArea, memSizeVerts);
	allocateArray((void**)&m_vdColors, memSizeVerts);
	cutilCheckError(cutCreateTimer(&m_timer));
}
void GPUVCAOUtil::FinalizeGPU()
{
	freeArray(m_vdPos);
	freeArray(m_vdFaces);
	freeArray(m_vdNor);
	freeArray(m_vdArea);
	freeArray(m_vdColors);
	cudaThreadExit();
}
//Get Face area
////////////////
//***************************************************************************
//
// Prepare host data
//
//***************************************************************************
void GPUVCAOUtil::PrePareHost()
{
	Mesh* mesh;
	uint totalVerts=0;
	uint totalFaces=0;
	meshNodes.ZeroCount();
	meshNodes.Shrink();
	//get the mesh nodes selected
	for (int i=0; i<ip->GetSelNodeCount(); i++) {
		INode* node=ip->GetSelNode(i);
		ObjectState os = node->EvalWorldState(ip->GetTime());
		if (os.obj && os.obj->SuperClassID()==GEOMOBJECT_CLASS_ID) {
			MeshInstance* inst = new MeshInstance(node);
			meshNodes.Append(1, &inst, 50);
		}
	}

	for (int i=0; i<meshNodes.Count(); i++)
    {
		MeshInstance* mi = meshNodes[i];
		mesh=mi->mesh;
		totalVerts+=mesh->numVerts;
		totalFaces+=mesh->numFaces;
    }
	InitMemory(totalVerts,totalFaces);
	//setup parameter
	m_params.TotalVerts=totalVerts;
	m_params.TotalFaces=totalFaces;
	m_params.Distance=iDistanceSpin->GetFVal();
	m_params.Passes=iPassesSpin->GetIVal();
	/////////////////
	int CurVertIdx=0;
	int curFaceIdx=0;
	int curMeshVertStartIdx=0;
	for (int i=0; i<meshNodes.Count(); i++)
	{
		INode* node = meshNodes[i]->node;
		Matrix3 tmWSM=node->GetObjTMAfterWSM(ip->GetTime());
		mesh=meshNodes[i]->mesh;
		int numVerts=mesh->numVerts;
		int numFaces=mesh->numFaces;
		//process the verts
#ifdef _DEBUG	
		char myString[1024];
#endif
		for (int j=0;j<numVerts;j++)
		{
			//get the vertex position and store
			Point3 curVert=mesh->verts[j];
			m_vhPos[CurVertIdx*3]=curVert.x;
			m_vhPos[CurVertIdx*3+1]=curVert.y;
			m_vhPos[CurVertIdx*3+2]=curVert.z;
#ifdef _DEBUG			
			
			sprintf( myString, "VID:%d pos:(%.4f, %.4f, %.4f)\n", 
				CurVertIdx, m_vhPos[CurVertIdx*3], m_vhPos[CurVertIdx*3+1], m_vhPos[CurVertIdx*3+2]);
			OutputDebugString(myString) ;

#endif
			CurVertIdx++;
		}
		//process the faces
		for (int j=0;j<numFaces;j++)
		{
			//get the vert id in the face
			m_vhFaces[curFaceIdx*3]=curMeshVertStartIdx+mesh->faces[j].v[0];
			m_vhFaces[curFaceIdx*3+1]=curMeshVertStartIdx+mesh->faces[j].v[1];
			m_vhFaces[curFaceIdx*3+2]=curMeshVertStartIdx+mesh->faces[j].v[2];
#ifdef _DEBUG			
/*			sprintf( myString, "FID:%d VIF:(%d, %d, %d)\n", 
				curFaceIdx, m_vhFaces[curFaceIdx*3], m_vhFaces[curFaceIdx*3+1], m_vhFaces[curFaceIdx*3+2]);
			OutputDebugString(myString) ;
*/
#endif
			curFaceIdx++;
		}
		curMeshVertStartIdx=CurVertIdx;
	}
	ip->RedrawViews(ip->GetTime());
}

////////////////////////////////
//Do on the device
///////////////////////////////
void GPUVCAOUtil::DeviceWork()
{
	//get time 
	_SYSTEMTIME sTime1;
	_SYSTEMTIME sTime2;
	
	//right now if the verts are exceeding a limit it will crash
	//so let's put a threshold
	if (m_params.TotalVerts==0||m_params.TotalFaces==0||m_params.TotalVerts>3000000)
		return;
	InitGPU(m_params.TotalVerts,m_params.TotalFaces);
	setParameters(&m_params);
	/////////////
	UINT memSizeVerts=m_params.TotalVerts*sizeof(float);
	UINT memSizeFaces=m_params.TotalFaces*sizeof(uint);
	copyArrayToDevice(m_vdPos, m_vhPos, 0, memSizeVerts*3);
	copyArrayToDevice(m_vdFaces, m_vhFaces, 0,memSizeFaces*3);
	copyArrayToDevice(m_vdColors, m_vhColors, 0, memSizeVerts);
	cutilCheckError(cutCreateTimer(&m_timer));
	/////////////
	//run it
	/////////////
	
	//time
	GetSystemTime(&sTime1);

	prepareMeshSystem(m_vdPos,m_vdFaces,m_vdNor,m_vdArea,m_params.TotalVerts);
	copyArrayFromDevice(m_vhNor,m_vdNor,memSizeVerts*3);
	copyArrayFromDevice(m_vhArea,m_vdArea,memSizeVerts);
	
	char myString[512];
#ifdef _DEBUG
/*	
	for(int i=0;i<m_params.TotalVerts;i++)
	{
	
		sprintf( myString, "Before:VID:%d Normal:(%.4f, %.4f, %.4f) Area:%.4f\n", 
			i,m_vhNor[i*3],m_vhNor[i*3+1],m_vhNor[i*3+2],m_vhArea[i]);
		OutputDebugString(myString) ;
	}
*/
#endif

	integrateSystem(m_vdPos,m_vdNor,m_vdArea,m_vdColors,m_params.TotalVerts,m_params.Passes);
	//copy device back to host
	copyArrayFromDevice(m_vhColors,m_vdColors,memSizeVerts);
	copyArrayFromDevice(m_vhNor,m_vdNor,memSizeVerts*3);
	copyArrayFromDevice(m_vhArea,m_vdArea,memSizeVerts);
	//time
	GetSystemTime(&sTime2);
	int MiSecInv=(sTime2.wMinute*60+sTime2.wSecond)*1000+sTime2.wMilliseconds-(sTime1.wMinute*60+sTime1.wSecond)*1000-sTime1.wMilliseconds;
	float SecInv=(float)MiSecInv/1000.0f;
	sprintf( myString, "TotalTime:%.4f Sec",SecInv);
	ip->DisplayTempPrompt(myString,7000);

#ifdef _DEBUG
/*
	for(int i=0;i<m_params.TotalVerts;i++)
	{
		sprintf( myString, "After:VID:%d color:(%.4f) Normal:(%.4f, %.4f, %.4f) Area:%.4f\n", 
			i,m_vhColors[i],m_vhNor[i*3],m_vhNor[i*3+1],m_vhNor[i*3+2],m_vhArea[i]);

		OutputDebugString(myString) ;
	}
*/
#endif

	cutilCheckError(cutCreateTimer(&m_timer));
	FinalizeGPU();
	
}
////////////////////////////////
//Do on the CPU
///////////////////////////////
void GPUVCAOUtil::getNormalArea()
{
	for (uint v=0;v<m_params.TotalVerts;v++)
	{
		Point3 iNormal = Point3(0.0f, 0.0f, 0.0f);
		int numNormals = 0;
		float PArea=0.0f;

		for (uint f = 0; f < m_params.TotalFaces; f++) {
			for (int fi = 0; fi < 3; fi++) {
				if (m_vhFaces[f*3+fi]== v) {
					Point3 V0=Point3(m_vhPos[m_vhFaces[f*3]*3],m_vhPos[m_vhFaces[f*3]*3+1],m_vhPos[m_vhFaces[f*3]*3+2]);
					Point3 V1=Point3(m_vhPos[m_vhFaces[f*3+1]*3],m_vhPos[m_vhFaces[f*3+1]*3+1],m_vhPos[m_vhFaces[f*3+1]*3+2]);
					Point3 V2=Point3(m_vhPos[m_vhFaces[f*3+2]*3],m_vhPos[m_vhFaces[f*3+2]*3+1],m_vhPos[m_vhFaces[f*3+2]*3+2]);
					Point3 fNormal=(V1-V0)^(V2-V1); 
					PArea+=fNormal.Length()/2.0f;
					iNormal += fNormal;
					numNormals++;
				}
			}
		}
		if (numNormals==0) 
			numNormals=1;

		iNormal=iNormal.Normalize();
		m_vhNor[v*3]= iNormal.x;
		m_vhNor[v*3+1]= iNormal.y;
		m_vhNor[v*3+2]= iNormal.z;
		m_vhArea[v]=PArea/((float)numNormals*PI);
	}
}
float saturate(float inVal)
{
	float retV=inVal<0.0f?0.0f:(inVal>1.0f?1.0f:inVal);
	return retV;
}
void GPUVCAOUtil::CPUWork()
{
	//get time 
	_SYSTEMTIME sTime1;
	_SYSTEMTIME sTime2;
	char myString[512];
	//time
	GetSystemTime(&sTime1);
	//get all the normal and area
	getNormalArea();
#ifdef _DEBUG
/*	for(int i=0;i<m_params.TotalVerts;i++)
	{
		sprintf( myString, "Before:VID:%d Normal:(%.4f, %.4f, %.4f) Area:%.4f\n", 
		i,m_vhNor[i*3],m_vhNor[i*3+1],m_vhNor[i*3+2],m_vhArea[i]);
		OutputDebugString(myString) ;
	}
*/
#endif
	
	
	//get all AO done on cpu
	for(INT pass=1;pass<=m_params.Passes;pass++)
	{
		for(uint curV=0;curV<m_params.TotalVerts;curV++)
		{
			const Point3 hPosV=Point3 (m_vhPos[curV*3],m_vhPos[curV*3+1],m_vhPos[curV*3+2]);
			Point3 hNorV=Point3 (m_vhNor[curV*3],m_vhNor[curV*3+1],m_vhNor[curV*3+2]);
			const Point3 hNorVConst=hNorV;
			Point3 v;
			float d2;
			float value;
			float total=0.0f; 

			for(uint i=0;i<m_params.TotalVerts;i++)
			{
				const Point3 hPosI=Point3 (m_vhPos[i*3],m_vhPos[i*3+1],m_vhPos[i*3+2]);
				const Point3 hNorI=Point3 (m_vhNor[i*3],m_vhNor[i*3+1],m_vhNor[i*3+2]);
				v=hPosI-hPosV;
				if (v.Length()>m_params.Distance||v.Length()==0)
					continue;
				d2=DotProd(v,v)+1e-7;
				if(d2<-4*m_vhArea[i])
				{
					m_vhArea[i]=0.0f;
				}
				v*=1.0f/sqrt(d2);
				float value1=saturate(DotProd(hNorI, v));
				float value2=saturate(3.0f*DotProd(hNorVConst, v));
				value=(1.0f -1.0f/sqrt(abs(m_vhArea[i])/d2 + 1.0f))*value1*value2;
				if (pass==2)
					value*=m_vhColors[i];
				hNorV-= value*v;
				total += value;
			}
			m_vhNor[curV*3]=hNorV.x;m_vhNor[curV*3+1]=hNorV.y;m_vhNor[curV*3+2]=hNorV.z;
			if (pass==1)
				m_vhColors[curV] = saturate(1.0f-total);
			else
				m_vhColors[curV] = m_vhColors[curV]*0.4f+ saturate(1.0f - total)*0.6f;
		}
	}
	//time
	GetSystemTime(&sTime2);
	int MiSecInv=(sTime2.wMinute*60+sTime2.wSecond)*1000+sTime2.wMilliseconds-(sTime1.wMinute*60+sTime1.wSecond)*1000-sTime1.wMilliseconds;
	float SecInv=(float)MiSecInv/1000.0f;
	sprintf( myString, "TotalTime:%.4f Sec",SecInv);
	ip->DisplayTempPrompt(myString,7000);

#ifdef _DEBUG
/*
	for(int i=0;i<m_params.TotalVerts;i++)
	{
	sprintf( myString, "After:VID:%d color:(%.4f) Normal:(%.4f, %.4f, %.4f) Area:%.4f\n", 
	i,m_vhColors[i],m_vhNor[i*3],m_vhNor[i*3+1],m_vhNor[i*3+2],m_vhArea[i]);

	OutputDebugString(myString) ;
	}
*/
#endif


}
//////////////////////////////
///Apply Vertex Color
///////////////////////
int GPUVCAOUtil::ApplySelected()
{
	Tab<INode*> nodesTab;
	Tab<INode*>* nodesTabP;
	Mesh* mesh;
	Modifier* mod=NULL; //modifier to apply to the nodes, if needed
	IVertexPaint_R7* ivertexPaint=NULL; //the interface to the modifier

	for (int i=0; i<meshNodes.Count(); i++)
	{
		INode* node = meshNodes[i]->node;
		nodesTab.Append(1,&node);
	}
	///Add Modifier
	mod = (Modifier*)CreateInstance(OSM_CLASS_ID, PAINTLAYERMOD_CLASS_ID);
	if (mod != NULL) {
		ivertexPaint = (IVertexPaint_R7*)mod->GetInterface(IVERTEXPAINT_R7_INTERFACE_ID);
		if (ivertexPaint)
			ivertexPaint->SetOptions2(currentOptions);
	}
	if( ivertexPaint==NULL ) {
		if( mod!=NULL ) mod->DeleteThis();
		return 0;
	}
	theHold.Begin();
	nodesTabP=&nodesTab;
	if( mod!=NULL ) { // need to put the vertex paint modifier on the nodes
		AddModifier(*nodesTabP, mod );
		recentMod=mod;
	}
	//apply color to each node
	int CurVertIdx=0;
	for (int i=0; i<meshNodes.Count(); i++)
	{
		INode* node = meshNodes[i]->node;
		mesh=meshNodes[i]->mesh;
		ColorTab mixedVertexColors;
		mixedVertexColors.ZeroCount();
		mixedVertexColors.Shrink();
		int numVerts=mesh->numVerts;
		for (int j=0;j<numVerts;j++)
		{
			Color* vxCol = new Color(m_vhColors[CurVertIdx],m_vhColors[CurVertIdx],m_vhColors[CurVertIdx]);
			mixedVertexColors.Append(1,&vxCol,100);
			CurVertIdx++;	
		}
		if( ivertexPaint!=NULL&&mixedVertexColors.Count()) {
			ivertexPaint->SetColors( node, mixedVertexColors);
		}
	}

	theHold.Accept( GetString(IDS_HOLDMSG_APPLYVTXCOLOR) );
	UpdateUI();
	//free the mesh instance
	for (int i=0;i<meshNodes.Count();i++)
	{
		delete meshNodes[i];
	}
	meshNodes.ZeroCount();
	meshNodes.Shrink();

	_finalize();

	return 1;
}
//release memory
void GPUVCAOUtil::_finalize()
{
	//m_params.TotalVerts=0;
	//free the host array
	delete [] m_vhPos;
	delete [] m_vhFaces;
	delete [] m_vhNor;
	delete [] m_vhArea;
	delete [] m_vhColors;

}
////////////////////////////
//Modifier functions
///////////////////////////
DWORD WINAPI dummy(LPVOID arg) 
{
    return(0);
}
Modifier* GPUVCAOUtil::GetModifier(INode* node, Class_ID modCID)
{
	Object* obj = node->GetObjectRef();

	if (!obj)
		return NULL;

	ObjectState os = node->EvalWorldState(0);
	if (os.obj && os.obj->SuperClassID() != GEOMOBJECT_CLASS_ID) {
		return NULL;
	}

	// For all derived objects (can be > 1)
	while (obj && (obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)) {
		IDerivedObject* dobj = (IDerivedObject*)obj;
		int m;
		int numMods = dobj->NumModifiers();
		// Step through all modififers and verify the class id
		for (m=0; m<numMods; m++) {
			Modifier* mod = dobj->GetModifier(m);
			if (mod) {
				if (mod->ClassID() == modCID) {
					// Match! Return it
					return mod;
				}
			}
		}
		obj = dobj->GetObjRef();
	}

	return NULL;
}

void GPUVCAOUtil::AddModifier(Tab<INode*>& nodes, Modifier* mod) {
	theHold.Begin();

	for( int i=0; i<nodes.Count(); i++ ) {
		if( nodes[i]==NULL ) continue;
		Object* obj = nodes[i]->GetObjectRef();
		IDerivedObject* dobj = CreateDerivedObject(obj);
		//dobj->SetAFlag(A_LOCK_TARGET); //FIXME: needed?
		dobj->AddModifier(mod);
		//dobj->ClearAFlag(A_LOCK_TARGET); //FIXME: needed?
		nodes[i]->SetObjectRef(dobj);
	}

	theHold.Accept( GetString(IDS_HOLDMSG_ADDMODIFIER) );
}

void GPUVCAOUtil::DeleteModifier( INode* node, Modifier* mod ) {
	//for( int i=0; i<nodes.Count(); i++ ) {
	Object* obj =  node->GetObjectRef(); //nodes[i]->GetObjectRef();
	if (!obj) return;

	theHold.Begin();

	// For all derived objects (can be > 1)
	while (obj && (obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)) {
		IDerivedObject* dobj = (IDerivedObject*)obj;
		int numMods = dobj->NumModifiers();
		// Step through all modififers
		for (int j=(numMods-1); j>=0; j--) {
			Modifier* curMod = dobj->GetModifier(j);
			if( curMod==mod )
				dobj->DeleteModifier(j);
		}
		obj = dobj->GetObjRef();
	}
	//}

	theHold.Accept( GetString(IDS_HOLDMSG_DELETEMODIFIER) ); //FIXME: localize
}

BOOL GPUVCAOUtil::GetChannelName( int index, TSTR& name ) {
	Interface* ip = GetCOREInterface();
	int nodeCount = ip->GetSelNodeCount();

	TCHAR propKey[16];
	_stprintf(propKey, _T("MapChannel:%i"), index );

	//get the channel name for the first selected obejct
	INode* node;
	BOOL ok = (
		(nodeCount>0) &&
		((node=ip->GetSelNode(0))!=NULL) &&
		(node->GetUserPropString( propKey, name )) );

	//check the channel name for each other selected object...
	for( int i=1; ok && i<nodeCount; i++ ) {
		TSTR nextName; 
		INode* node = ip->GetSelNode(i);
		ok = node->GetUserPropString( propKey, nextName );
		//...we'll return a default value unless all the objects have a maching channel name
		if( nextName!=name ) ok=FALSE;
	}

	return ok;
}

MeshInstance::MeshInstance(INode* node)
{
	mesh = NULL;
	int	v;

	this->node = node;

	BOOL	deleteIt;
	TriObject* tri = GetTriObjectFromNode(node, GetCOREInterface()->GetTime(), deleteIt);
	if (tri) {
		mesh = new Mesh;
		*mesh = *&tri->GetMesh();

		Point3 vx;
		Matrix3 objToWorld = node->GetObjTMAfterWSM(GetCOREInterface()->GetTime());

		negParity = TMNegParity(objToWorld);

		mesh->buildRenderNormals();

		// Transform the vertices
		for (v=0; v<mesh->numVerts; v++) {
			vx =  objToWorld * mesh->getVert(v);
			mesh->setVert(v, vx);
		}

		Matrix3 normalObjToWorld(1);
		// Calculate the inverse-transpose of objToWorld for transforming normals.
		for (int it=0; it<3; it++) {
			Point4 p = Inverse(objToWorld).GetColumn(it);
			normalObjToWorld.SetRow(it,Point3(p[0],p[1],p[2]));
		}

		// Transform the face normals
		for (int nf = 0; nf < mesh->numFaces; nf++) {
			Point3	fn = mesh->getFaceNormal(nf);
			Point3	nfn = VectorTransform(normalObjToWorld, fn);
			mesh->setFaceNormal(nf, nfn);
		}

		boundingBox = mesh->getBoundingBox();

		// Get the bounding sphere
		center = 0.5f*(boundingBox.pmin+boundingBox.pmax);
		radsq = 0.0f;
		Point3 d;
		float nr;
		for (v= 0; v<mesh->numVerts; v++) {
			d = mesh->verts[v] - center;
			nr = DotProd(d,d);
			if (nr>radsq) radsq = nr;
		}

		if (deleteIt) {
			delete tri;
		}
	}
}

MeshInstance::~MeshInstance()
{
	if (mesh) {
		mesh->DeleteThis();
	}
}
BOOL MeshInstance::TMNegParity(Matrix3 &m)
{
	return (DotProd(CrossProd(m.GetRow(0),m.GetRow(1)),m.GetRow(2))<0.0)?1:0;
}
TriObject* MeshInstance::GetTriObjectFromNode(INode *node, TimeValue t, int &deleteIt)
{
	deleteIt = FALSE;
	Object *obj = node->EvalWorldState(t).obj;

	if (obj->CanConvertToType(Class_ID(TRIOBJ_CLASS_ID, 0))) { 
		TriObject *tri = (TriObject *) obj->ConvertToType(t, 
			Class_ID(TRIOBJ_CLASS_ID, 0));
		// Note that the TriObject should only be deleted
		// if the pointer to it is not equal to the object
		// pointer that called ConvertToType()
		if (obj != tri) deleteIt = TRUE;
		return tri;
	}
	else {
		return NULL;
	}
}