// GaussianSplatNiagaraDataInterface.cpp
// Implementation of the Gaussian Splat Niagara Data Interface

#include "GaussianSplatNiagaraDataInterface.h"
#include "PLYParser.h"
#include "NiagaraTypes.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraRenderer.h"
#include "ShaderParameterUtils.h"

#define LOCTEXT_NAMESPACE "GaussianSplatNiagaraDataInterface"

// Function names
const FString UGaussianSplatNiagaraDataInterface::GetSplatCountFunctionName = TEXT("GetSplatCount");
const FString UGaussianSplatNiagaraDataInterface::GetPositionFunctionName = TEXT("GetSplatPosition");
const FString UGaussianSplatNiagaraDataInterface::GetScaleFunctionName = TEXT("GetSplatScale");
const FString UGaussianSplatNiagaraDataInterface::GetOrientationFunctionName = TEXT("GetSplatOrientation");
const FString UGaussianSplatNiagaraDataInterface::GetOpacityFunctionName = TEXT("GetSplatOpacity");
const FString UGaussianSplatNiagaraDataInterface::GetColorFunctionName = TEXT("GetSplatColor");

// Shader parameter names
const FString UGaussianSplatNiagaraDataInterface::SplatsCountParamName = TEXT("_SplatsCount");
const FString UGaussianSplatNiagaraDataInterface::GlobalTintParamName = TEXT("_GlobalTint");
const FString UGaussianSplatNiagaraDataInterface::PositionsBufferName = TEXT("_Positions");
const FString UGaussianSplatNiagaraDataInterface::ScalesBufferName = TEXT("_Scales");
const FString UGaussianSplatNiagaraDataInterface::OrientationsBufferName = TEXT("_Orientations");
const FString UGaussianSplatNiagaraDataInterface::SHZeroCoeffsBufferName = TEXT("_SHZeroCoeffsAndOpacity");

// VM function binders
DEFINE_NDI_DIRECT_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatCount);
DEFINE_NDI_DIRECT_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatPosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatScale);
DEFINE_NDI_DIRECT_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatOrientation);
DEFINE_NDI_DIRECT_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatOpacity);
DEFINE_NDI_DIRECT_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatColor);

UGaussianSplatNiagaraDataInterface::UGaussianSplatNiagaraDataInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, GlobalTint(FLinearColor::White)
	, bGPUDataDirty(true)
{
	Proxy.Reset(new FNDIGaussianSplatProxy());
	SplatData = MakeShared<TArray<FGaussianSplatData>, ESPMode::ThreadSafe>();
}

void UGaussianSplatNiagaraDataInterface::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags DIFlags =
			ENiagaraTypeRegistryFlags::AllowAnyVariable |
			ENiagaraTypeRegistryFlags::AllowParameter;

		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), DIFlags);
	}
	else
	{
		// Create dummy buffers for non-CDO instances
		// This ensures shaders always have valid buffers to bind
		PrepareGPUData();
	}

	MarkRenderDataDirty();
}

void UGaussianSplatNiagaraDataInterface::PostLoad()
{
	Super::PostLoad();
	MarkRenderDataDirty();
}

void UGaussianSplatNiagaraDataInterface::BeginDestroy()
{
	if (FNDIGaussianSplatProxy* SplatProxy = static_cast<FNDIGaussianSplatProxy*>(Proxy.Get()))
	{
		SplatProxy->ReleaseBuffers();
	}
	Super::BeginDestroy();
}

bool UGaussianSplatNiagaraDataInterface::LoadFromPLYFile(const FString& FilePath)
{
	FPLYParser Parser;
	TArray<FGaussianSplatData> ParsedSplats;

	if (!Parser.ParseFile(FilePath, ParsedSplats))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load PLY file: %s"), *FilePath);
		return false;
	}

	//    We do NOT touch the old memory (which the Render Thread might be reading).
	FSplatDataPtr NewData = MakeShared<TArray<FGaussianSplatData>, ESPMode::ThreadSafe>(MoveTemp(ParsedSplats));

	//    If the Render Thread is holding the old pointer, it stays valid until the RT releases it.
	//    If no one is holding the old pointer, it gets deleted immediately.
	SplatData = NewData;

	// Update tracking var for Editor visibility
	CurrentSplatCount = SplatData->Num(); 

	MarkRenderDataDirty();
	UE_LOG(LogTemp, Log, TEXT("Loaded %d splats from PLY file"), GetSplatCount());
	return true;
}

int32 UGaussianSplatNiagaraDataInterface::GetSplatCount() const 
{ 
	return SplatData.IsValid() ? SplatData->Num() : 0; 
}

void UGaussianSplatNiagaraDataInterface::ClearSplats()
{
	// Create a new empty array wrapped in a shared ptr
	SplatData = MakeShared<TArray<FGaussianSplatData>, ESPMode::ThreadSafe>();
    
	CurrentSplatCount = 0;
	MarkRenderDataDirty();
}


bool UGaussianSplatNiagaraDataInterface::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
    
	UGaussianSplatNiagaraDataInterface* DestNDI = Cast<UGaussianSplatNiagaraDataInterface>(Destination);
	if (!DestNDI)
	{
		return false;
	}
	if (SplatData.IsValid() && SplatData->Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CopyToInternal: Transferring %d splats to Simulation"), SplatData->Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT(" CopyToInternal: Cloning EMPTY data! (Source has 0 splats)"));
	}
	DestNDI->SplatData = SplatData; 
	DestNDI->GlobalTint = GlobalTint;
	DestNDI->CurrentSplatCount = GetSplatCount();
	DestNDI->MarkRenderDataDirty();
    
	return true;
}

bool UGaussianSplatNiagaraDataInterface::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other)) return false;
    
	// Always force refresh to ensure updates propagate
	return false; 
}


/*bool UGaussianSplatNiagaraDataInterface::CanExecuteOnTarget(ENiagaraSimTarget Target) const
{
	// Support both CPU and GPU execution
	return true;
}*/

void UGaussianSplatNiagaraDataInterface::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	// GetSplatCount - returns total number of splats
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = *GetSplatCountFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("GaussianSplatNDI")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	// GetSplatPosition - returns position for a given splat index
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = *GetPositionFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("GaussianSplatNDI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	// GetSplatScale
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = *GetScaleFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("GaussianSplatNDI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Scale")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	// GetSplatOrientation
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = *GetOrientationFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("GaussianSplatNDI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Orientation")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	// GetSplatOpacity
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = *GetOpacityFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("GaussianSplatNDI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Opacity")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	// GetSplatColor
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = *GetColorFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("GaussianSplatNDI")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}

void UGaussianSplatNiagaraDataInterface::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	if (BindingInfo.Name == *GetSplatCountFunctionName)
	{
		NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatCount)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == *GetPositionFunctionName)
	{
		NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatPosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == *GetScaleFunctionName)
	{
		NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatScale)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == *GetOrientationFunctionName)
	{
		NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatOrientation)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == *GetOpacityFunctionName)
	{
		NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatOpacity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == *GetColorFunctionName)
	{
		NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatColor)::Bind(this, OutFunc);
	}
}

// CPU VM Function implementations

void UGaussianSplatNiagaraDataInterface::GetSplatCount(FVectorVMExternalFunctionContext& Context) const
{
    FNDIOutputParam<int32> OutCount(Context);
    
    const int32 SplatCount = SplatData.IsValid() ? SplatData->Num() : 0;
    
    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
       OutCount.SetAndAdvance(SplatCount);
    }
}


void UGaussianSplatNiagaraDataInterface::GetSplatPosition(FVectorVMExternalFunctionContext& Context) const
{
    VectorVM::FUserPtrHandler<UGaussianSplatNiagaraDataInterface> InstData(Context);
    FNDIInputParam<int32> IndexParam(Context);
    FNDIOutputParam<float> OutPosX(Context);
    FNDIOutputParam<float> OutPosY(Context);
    FNDIOutputParam<float> OutPosZ(Context);
    
    const bool bHasData = SplatData.IsValid();
    const TArray<FGaussianSplatData>* SplatsPtr = bHasData ? SplatData.Get() : nullptr;

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
       const int32 Index = IndexParam.GetAndAdvance();
       
       if (bHasData && SplatsPtr->IsValidIndex(Index))
       {
          const FGaussianSplatData& Splat = (*SplatsPtr)[Index];
          OutPosX.SetAndAdvance(Splat.Position.X);
          OutPosY.SetAndAdvance(Splat.Position.Y);
          OutPosZ.SetAndAdvance(Splat.Position.Z);
       }
       else
       {
          OutPosX.SetAndAdvance(0.0f);
          OutPosY.SetAndAdvance(0.0f);
          OutPosZ.SetAndAdvance(0.0f);
       }
    }
}


void UGaussianSplatNiagaraDataInterface::GetSplatScale(FVectorVMExternalFunctionContext& Context) const
{
    VectorVM::FUserPtrHandler<UGaussianSplatNiagaraDataInterface> InstData(Context);
    FNDIInputParam<int32> IndexParam(Context);
    FNDIOutputParam<float> OutScaleX(Context);
    FNDIOutputParam<float> OutScaleY(Context);
    FNDIOutputParam<float> OutScaleZ(Context);

    const bool bHasData = SplatData.IsValid();
    const TArray<FGaussianSplatData>* SplatsPtr = bHasData ? SplatData.Get() : nullptr;

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
       const int32 Index = IndexParam.GetAndAdvance();

       if (bHasData && SplatsPtr->IsValidIndex(Index))
       {
          const FGaussianSplatData& Splat = (*SplatsPtr)[Index];
          OutScaleX.SetAndAdvance(Splat.Scale.X);
          OutScaleY.SetAndAdvance(Splat.Scale.Y);
          OutScaleZ.SetAndAdvance(Splat.Scale.Z);
       }
       else
       {
          // Default scale is usually 1,1,1
          OutScaleX.SetAndAdvance(1.0f);
          OutScaleY.SetAndAdvance(1.0f);
          OutScaleZ.SetAndAdvance(1.0f);
       }
    }
}


void UGaussianSplatNiagaraDataInterface::GetSplatOrientation(FVectorVMExternalFunctionContext& Context) const
{
    VectorVM::FUserPtrHandler<UGaussianSplatNiagaraDataInterface> InstData(Context);
    FNDIInputParam<int32> IndexParam(Context);
    FNDIOutputParam<float> OutQuatX(Context);
    FNDIOutputParam<float> OutQuatY(Context);
    FNDIOutputParam<float> OutQuatZ(Context);
    FNDIOutputParam<float> OutQuatW(Context);

    const bool bHasData = SplatData.IsValid();
    const TArray<FGaussianSplatData>* SplatsPtr = bHasData ? SplatData.Get() : nullptr;

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
       const int32 Index = IndexParam.GetAndAdvance();

       if (bHasData && SplatsPtr->IsValidIndex(Index))
       {
          const FGaussianSplatData& Splat = (*SplatsPtr)[Index];
          OutQuatX.SetAndAdvance(Splat.Orientation.X);
          OutQuatY.SetAndAdvance(Splat.Orientation.Y);
          OutQuatZ.SetAndAdvance(Splat.Orientation.Z);
          OutQuatW.SetAndAdvance(Splat.Orientation.W);
       }
       else
       {
          // Default Quaternion Identity (0,0,0,1)
          OutQuatX.SetAndAdvance(0.0f);
          OutQuatY.SetAndAdvance(0.0f);
          OutQuatZ.SetAndAdvance(0.0f);
          OutQuatW.SetAndAdvance(1.0f);
       }
    }
}


void UGaussianSplatNiagaraDataInterface::GetSplatOpacity(FVectorVMExternalFunctionContext& Context) const
{
    VectorVM::FUserPtrHandler<UGaussianSplatNiagaraDataInterface> InstData(Context);
    FNDIInputParam<int32> IndexParam(Context);
    FNDIOutputParam<float> OutOpacity(Context);

    const bool bHasData = SplatData.IsValid();
    const TArray<FGaussianSplatData>* SplatsPtr = bHasData ? SplatData.Get() : nullptr;

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
       const int32 Index = IndexParam.GetAndAdvance();

       if (bHasData && SplatsPtr->IsValidIndex(Index))
       {
          OutOpacity.SetAndAdvance((*SplatsPtr)[Index].Opacity);
       }
       else
       {
          OutOpacity.SetAndAdvance(0.0f);
       }
    }
}

void UGaussianSplatNiagaraDataInterface::GetSplatColor(FVectorVMExternalFunctionContext& Context) const
{
	VectorVM::FUserPtrHandler<UGaussianSplatNiagaraDataInterface> InstData(Context);
	FNDIInputParam<int32> IndexParam(Context);
	FNDIOutputParam<float> OutColorR(Context);
	FNDIOutputParam<float> OutColorG(Context);
	FNDIOutputParam<float> OutColorB(Context);
	FNDIOutputParam<float> OutColorA(Context);

    const bool bHasData = SplatData.IsValid();
    const TArray<FGaussianSplatData>* SplatsPtr = bHasData ? SplatData.Get() : nullptr;

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		const int32 Index = IndexParam.GetAndAdvance();

		if (bHasData && SplatsPtr->IsValidIndex(Index))
		{
            const FGaussianSplatData& Splat = (*SplatsPtr)[Index];
			FLinearColor Color = FGaussianSplatData::SHToColor(Splat.ZeroOrderHarmonicsCoefficients);
			Color *= GlobalTint;
            
			OutColorR.SetAndAdvance(Color.R);
			OutColorG.SetAndAdvance(Color.G);
			OutColorB.SetAndAdvance(Color.B);
			OutColorA.SetAndAdvance(Splat.Opacity);
		}
		else
		{
			OutColorR.SetAndAdvance(0.0f);
			OutColorG.SetAndAdvance(0.0f);
			OutColorB.SetAndAdvance(0.0f);
			OutColorA.SetAndAdvance(0.0f);
		}
	}
}
// GPU Support
void UGaussianSplatNiagaraDataInterface::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FGaussianSplatShaderParameters>();
}

void UGaussianSplatNiagaraDataInterface::SetShaderParameters(
	const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	FGaussianSplatShaderParameters* ShaderParameters = 
		Context.GetParameterNestedStruct<FGaussianSplatShaderParameters>();
    
	if (!ShaderParameters) return;
    
	FNDIGaussianSplatProxy& DIProxy = Context.GetProxy<FNDIGaussianSplatProxy>();
    
	// Access data via Shared Pointer
	int32 SplatCount = 0;
	if (SplatData.IsValid())
	{
		SplatCount = SplatData->Num();
	}
    
	// Upload logic
	if (SplatCount > 0 && DIProxy.SplatsCount != SplatCount)
	{
		if (DIProxy.AreBuffersValid())
		{
			DIProxy.ReleaseBuffers();
		}
        
		DIProxy.InitializeBuffers(SplatCount);
        
		// Pass the array inside the shared pointer to the proxy
		DIProxy.UploadDataToGPU(*SplatData); 
        
		DIProxy.SplatsCount = SplatCount;
		DIProxy.GlobalTint = FVector3f(GlobalTint);
	}
    
	// Bind parameters
	ShaderParameters->SplatsCount = DIProxy.SplatsCount;
	ShaderParameters->GlobalTint = DIProxy.GlobalTint;
	ShaderParameters->Positions = DIProxy.PositionsBuffer.SRV;
	ShaderParameters->Scales = DIProxy.ScalesBuffer.SRV;
	ShaderParameters->Orientations = DIProxy.OrientationsBuffer.SRV;
	ShaderParameters->SHZeroCoeffsAndOpacity = DIProxy.SHZeroCoeffsAndOpacityBuffer.SRV;
}



void UGaussianSplatNiagaraDataInterface::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	// Define shader parameters
	OutHLSL.Appendf(TEXT("int %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *SplatsCountParamName);
	OutHLSL.Appendf(TEXT("float3 %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *GlobalTintParamName);
	OutHLSL.Appendf(TEXT("Buffer<float4> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *PositionsBufferName);
	OutHLSL.Appendf(TEXT("Buffer<float4> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *ScalesBufferName);
	OutHLSL.Appendf(TEXT("Buffer<float4> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *OrientationsBufferName);
	OutHLSL.Appendf(TEXT("Buffer<float4> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *SHZeroCoeffsBufferName);
}

bool UGaussianSplatNiagaraDataInterface::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if (Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL))
	{
		return true;
	}

	// GetSplatCount
	if (FunctionInfo.DefinitionName == *GetSplatCountFunctionName)
	{
		static const TCHAR* FormatHLSL = TEXT(R"(
			void {FunctionName}(out int OutCount)
			{
				OutCount = {SplatsCount};
			}
		)");

		const TMap<FString, FStringFormatArg> Args = {
			{TEXT("FunctionName"), FStringFormatArg(FunctionInfo.InstanceName)},
			{TEXT("SplatsCount"), FStringFormatArg(ParamInfo.DataInterfaceHLSLSymbol + SplatsCountParamName)},
		};
		OutHLSL += FString::Format(FormatHLSL, Args);
		return true;
	}

	// GetSplatPosition
	if (FunctionInfo.DefinitionName == *GetPositionFunctionName)
	{
		static const TCHAR* FormatHLSL = TEXT(R"(
			void {FunctionName}(int Index, out float3 OutPosition)
			{
				OutPosition = {PositionsBuffer}[Index].xyz;
			}
		)");

		const TMap<FString, FStringFormatArg> Args = {
			{TEXT("FunctionName"), FStringFormatArg(FunctionInfo.InstanceName)},
			{TEXT("PositionsBuffer"), FStringFormatArg(ParamInfo.DataInterfaceHLSLSymbol + PositionsBufferName)},
		};
		OutHLSL += FString::Format(FormatHLSL, Args);
		return true;
	}

	// GetSplatScale
	if (FunctionInfo.DefinitionName == *GetScaleFunctionName)
	{
		static const TCHAR* FormatHLSL = TEXT(R"(
			void {FunctionName}(int Index, out float3 OutScale)
			{
				OutScale = {ScalesBuffer}[Index].xyz;
			}
		)");

		const TMap<FString, FStringFormatArg> Args = {
			{TEXT("FunctionName"), FStringFormatArg(FunctionInfo.InstanceName)},
			{TEXT("ScalesBuffer"), FStringFormatArg(ParamInfo.DataInterfaceHLSLSymbol + ScalesBufferName)},
		};
		OutHLSL += FString::Format(FormatHLSL, Args);
		return true;
	}

	// GetSplatOrientation
	if (FunctionInfo.DefinitionName == *GetOrientationFunctionName)
	{
		static const TCHAR* FormatHLSL = TEXT(R"(
			void {FunctionName}(int Index, out float4 OutOrientation)
			{
				OutOrientation = {OrientationsBuffer}[Index];
			}
		)");

		const TMap<FString, FStringFormatArg> Args = {
			{TEXT("FunctionName"), FStringFormatArg(FunctionInfo.InstanceName)},
			{TEXT("OrientationsBuffer"), FStringFormatArg(ParamInfo.DataInterfaceHLSLSymbol + OrientationsBufferName)},
		};
		OutHLSL += FString::Format(FormatHLSL, Args);
		return true;
	}

	// GetSplatOpacity
	if (FunctionInfo.DefinitionName == *GetOpacityFunctionName)
	{
		static const TCHAR* FormatHLSL = TEXT(R"(
			void {FunctionName}(int Index, out float OutOpacity)
			{
				OutOpacity = {SHBuffer}[Index].w;
			}
		)");

		const TMap<FString, FStringFormatArg> Args = {
			{TEXT("FunctionName"), FStringFormatArg(FunctionInfo.InstanceName)},
			{TEXT("SHBuffer"), FStringFormatArg(ParamInfo.DataInterfaceHLSLSymbol + SHZeroCoeffsBufferName)},
		};
		OutHLSL += FString::Format(FormatHLSL, Args);
		return true;
	}

	// GetSplatColor
	if (FunctionInfo.DefinitionName == *GetColorFunctionName)
	{
		static const TCHAR* FormatHLSL = TEXT(R"(
			void {FunctionName}(int Index, out float4 OutColor)
			{
				float4 SHData = {SHBuffer}[Index];
				float3 SHCoeffs = SHData.xyz;
				float Opacity = SHData.w;
				
				// SH0 constant for base color calculation
				const float C0 = 0.28209479177387814;
				float3 BaseColor = SHCoeffs * C0 + 0.5;
				BaseColor = saturate(BaseColor);
				
				// Apply global tint
				BaseColor *= {GlobalTint};
				
				OutColor = float4(BaseColor, Opacity);
			}
		)");

		const TMap<FString, FStringFormatArg> Args = {
			{TEXT("FunctionName"), FStringFormatArg(FunctionInfo.InstanceName)},
			{TEXT("SHBuffer"), FStringFormatArg(ParamInfo.DataInterfaceHLSLSymbol + SHZeroCoeffsBufferName)},
			{TEXT("GlobalTint"), FStringFormatArg(ParamInfo.DataInterfaceHLSLSymbol + GlobalTintParamName)},
		};
		OutHLSL += FString::Format(FormatHLSL, Args);
		return true;
	}

	return false;
}

void UGaussianSplatNiagaraDataInterface::MarkRenderDataDirty()
{
	bGPUDataDirty = true;
}

void UGaussianSplatNiagaraDataInterface::PrepareGPUData()
{
    if (!SplatData.IsValid())
    {
        return;
    }

    FNDIGaussianSplatProxy* SplatProxy = static_cast<FNDIGaussianSplatProxy*>(Proxy.Get());
    if (!SplatProxy)
    {
        return;
    }

    const int32 Count = SplatData->Num();

if (Count > 0 && (!SplatProxy->AreBuffersValid() || SplatProxy->SplatsCount != Count))    {
         SplatProxy->InitializeBuffers(Count);
        
         SplatProxy->UploadDataToGPU(*SplatData); 
        
         SplatProxy->SplatsCount = Count;
         SplatProxy->GlobalTint = FVector3f(GlobalTint.R, GlobalTint.G, GlobalTint.B);
        
         UE_LOG(LogTemp, Log, TEXT("PrepareGPUData: Uploaded %d splats to GPU (Game Thread fallback)"), Count);
    }
    else if (Count == 0 && !SplatProxy->PositionsBuffer.IsValid())
    {
       SplatProxy->InitializeBuffers(1);
        
       TArray<FGaussianSplatData> DummyData;
       FGaussianSplatData DummySplat;
       DummySplat.Position = FVector::ZeroVector;
       DummySplat.Scale = FVector::OneVector;
       DummySplat.Orientation = FQuat::Identity;
       DummySplat.Opacity = 0.0f;
       DummySplat.ZeroOrderHarmonicsCoefficients = FVector::ZeroVector;
       DummyData.Add(DummySplat);
        
       SplatProxy->UploadDataToGPU(DummyData);
       SplatProxy->SplatsCount = 0; 
    }
}

bool UGaussianSplatNiagaraDataInterface::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	PrepareGPUData();
	return true; 
}

#undef LOCTEXT_NAMESPACE