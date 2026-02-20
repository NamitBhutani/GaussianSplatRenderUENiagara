#include "GaussianSplatNiagaraDataInterface.h"
#include "NiagaraParameterStore.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraTypes.h"
#include "PLYParser.h"
#include "ShaderParameterUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogGaussianSplat, Log, All);

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

// Construction & Lifecycle

UGaussianSplatNiagaraDataInterface::UGaussianSplatNiagaraDataInterface(const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer), GlobalTint(FLinearColor::White), bGPUDataDirty(false)
{
    Proxy.Reset(new FNDIGaussianSplatProxy());
    UE_LOG(LogGaussianSplat, Log, TEXT("[Constructor] %s | Proxy=%p"), *GetName(), Proxy.Get());
}

void UGaussianSplatNiagaraDataInterface::PostInitProperties()
{
    Super::PostInitProperties();

    const bool bIsCDO = HasAnyFlags(RF_ClassDefaultObject);
    UE_LOG(LogGaussianSplat, Log, TEXT("[PostInitProperties] %s | IsCDO=%d | Path='%s' | Splats=%d | Outer=%s"),
           *GetName(), bIsCDO, *PlyFilePath.FilePath, Splats.Num(), GetOuter() ? *GetOuter()->GetName() : TEXT("null"));

    if (bIsCDO)
    {
        ENiagaraTypeRegistryFlags DIFlags =
            ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
        FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), DIFlags);
        UE_LOG(LogGaussianSplat, Log, TEXT("[PostInitProperties] %s | Registered NDI type with Niagara"), *GetName());
    }

    MarkRenderDataDirty();

    if (!PlyFilePath.FilePath.IsEmpty())
    {
        UE_LOG(LogGaussianSplat, Log, TEXT("[PostInitProperties] %s | Path not empty, calling LoadPlyFile"),
               *GetName());
        LoadPlyFile();
    }
    else
    {
        UE_LOG(LogGaussianSplat, Log, TEXT("[PostInitProperties] %s | Path empty, skipped load"), *GetName());
    }
}

void UGaussianSplatNiagaraDataInterface::PostLoad()
{
    Super::PostLoad();

    const bool bHasPath = !PlyFilePath.FilePath.IsEmpty();
    const bool bHasSplats = Splats.Num() > 0;

    UE_LOG(LogGaussianSplat, Log,
           TEXT("[PostLoad] %s | Path='%s' | HasPath=%d | HasSplats=%d | SplatCount=%d | Outer=%s"), *GetName(),
           *PlyFilePath.FilePath, bHasPath, bHasSplats, Splats.Num(),
           GetOuter() ? *GetOuter()->GetName() : TEXT("null"));

    if (bHasPath && !bHasSplats)
    {
        UE_LOG(LogGaussianSplat, Log,
               TEXT("[PostLoad] %s | Path set but Splats empty (not serialized) — reloading from disk"), *GetName());
        LoadPlyFile();
    }
    else if (bHasPath && bHasSplats)
    {
        UE_LOG(LogGaussianSplat, Log,
               TEXT("[PostLoad] %s | Path set AND Splats already populated (%d) — skipped reload"), *GetName(),
               Splats.Num());
    }
    else
    {
        UE_LOG(LogGaussianSplat, Log, TEXT("[PostLoad] %s | No path set — nothing to load"), *GetName());
    }
}

#if WITH_EDITOR
void UGaussianSplatNiagaraDataInterface::PostEditChangeProperty(struct FPropertyChangedEvent &PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
    const FName MemberName =
        PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
    UE_LOG(LogGaussianSplat, Log,
           TEXT("[PostEditChangeProperty] %s | Property='%s' | MemberProperty='%s' | Path='%s' | Splats=%d"),
           *GetName(), *PropName.ToString(), *MemberName.ToString(), *PlyFilePath.FilePath, Splats.Num());

    if (MemberName == GET_MEMBER_NAME_CHECKED(UGaussianSplatNiagaraDataInterface, PlyFilePath))
    {
        UE_LOG(LogGaussianSplat, Log, TEXT("[PostEditChangeProperty] %s | PlyFilePath changed — calling LoadPlyFile"),
               *GetName());
        LoadPlyFile();
    }
    else if (PropName == GET_MEMBER_NAME_CHECKED(UGaussianSplatNiagaraDataInterface, GlobalTint))
    {
        UE_LOG(LogGaussianSplat, Log,
               TEXT("[PostEditChangeProperty] %s | GlobalTint changed to R=%.2f G=%.2f B=%.2f A=%.2f"), *GetName(),
               GlobalTint.R, GlobalTint.G, GlobalTint.B, GlobalTint.A);
    }
    else
    {
        UE_LOG(LogGaussianSplat, Log, TEXT("[PostEditChangeProperty] %s | Unhandled property '%s'"), *GetName(),
               *PropName.ToString());
    }
}
#endif

void UGaussianSplatNiagaraDataInterface::BeginDestroy()
{
    UE_LOG(LogGaussianSplat, Log, TEXT("[BeginDestroy] %s | Splats=%d"), *GetName(), Splats.Num());
    Super::BeginDestroy();
    UE_LOG(LogGaussianSplat, Log, TEXT("[BeginDestroy] %s | Complete"), *GetName());
}

// Data Loading

void UGaussianSplatNiagaraDataInterface::LoadPlyFile()
{
    FString FullPath = PlyFilePath.FilePath;
    UE_LOG(LogGaussianSplat, Log, TEXT("[LoadPlyFile] %s | Path='%s' | IsEmpty=%d"), *GetName(), *FullPath,
           FullPath.IsEmpty());

    if (FullPath.IsEmpty())
    {
        UE_LOG(LogGaussianSplat, Warning, TEXT("[LoadPlyFile] %s | Path is empty, aborting"), *GetName());
        return;
    }

    LoadFromPLYFile(FullPath);
}

bool UGaussianSplatNiagaraDataInterface::LoadFromPLYFile(const FString &FilePath)
{
    UE_LOG(LogGaussianSplat, Log, TEXT("[LoadFromPLYFile] %s | Attempting to load: '%s' | ExistingSplats=%d"),
           *GetName(), *FilePath, Splats.Num());

    FPLYParser Parser;
    TArray<FGaussianSplatData> ParsedSplats;
    if (!Parser.ParseFile(FilePath, ParsedSplats))
    {
        UE_LOG(LogGaussianSplat, Error, TEXT("[LoadFromPLYFile] %s | PARSE FAILED: %s"), *GetName(),
               *Parser.GetErrorMessage());
        return false;
    }

    const int32 ParsedCount = ParsedSplats.Num();
    Splats = MoveTemp(ParsedSplats);
    CurrentSplatCount = Splats.Num();

    UE_LOG(LogGaussianSplat, Log, TEXT("[LoadFromPLYFile] %s | PARSE OK: %d splats"), *GetName(), ParsedCount);

    if (Splats.Num() > 0)
    {
        const FGaussianSplatData &First = Splats[0];
        UE_LOG(LogGaussianSplat, Log,
               TEXT("[LoadFromPLYFile] %s | Splat[0]: Pos=(%.2f,%.2f,%.2f) Scale=(%.2f,%.2f,%.2f) Opacity=%.3f"),
               *GetName(), First.Position.X, First.Position.Y, First.Position.Z, First.Scale.X, First.Scale.Y,
               First.Scale.Z, First.Opacity);
    }

    MarkRenderDataDirty();
    // GPU upload now happens in InitPerInstanceData when a NiagaraComponent activates
    UE_LOG(LogGaussianSplat, Log,
           TEXT("[LoadFromPLYFile] %s | Data stored in Splats — GPU upload deferred to InitPerInstanceData"),
           *GetName());
    return true;
}

int32 UGaussianSplatNiagaraDataInterface::GetSplatCount() const
{
    return Splats.Num();
}

void UGaussianSplatNiagaraDataInterface::ClearSplats()
{
    Splats.Empty();
    CurrentSplatCount = 0;
    MarkRenderDataDirty();
}

bool UGaussianSplatNiagaraDataInterface::Equals(const UNiagaraDataInterface *Other) const
{
    if (!Super::Equals(Other))
        return false;

    const UGaussianSplatNiagaraDataInterface *OtherNDI = Cast<UGaussianSplatNiagaraDataInterface>(Other);
    if (!OtherNDI)
        return false;

    const bool bPathEqual = PlyFilePath.FilePath == OtherNDI->PlyFilePath.FilePath;
    const bool bTintEqual = GlobalTint == OtherNDI->GlobalTint;
    return bPathEqual && bTintEqual;
}

void UGaussianSplatNiagaraDataInterface::MarkRenderDataDirty()
{
    bGPUDataDirty = true;
}

bool UGaussianSplatNiagaraDataInterface::CopyToInternal(UNiagaraDataInterface *Destination) const
{
    if (!Super::CopyToInternal(Destination))
        return false;

    UGaussianSplatNiagaraDataInterface *DestNDI = Cast<UGaussianSplatNiagaraDataInterface>(Destination);
    if (!DestNDI)
        return false;

    // Copy all game-thread data; GPU upload is deferred to InitPerInstanceData
    DestNDI->PlyFilePath = PlyFilePath;
    DestNDI->GlobalTint = GlobalTint;
    DestNDI->Splats = Splats;
    DestNDI->CurrentSplatCount = CurrentSplatCount;
    DestNDI->MarkRenderDataDirty();

    UE_LOG(LogGaussianSplat, Log, TEXT("[CopyToInternal] %s -> %s | Path='%s' | Splats=%d | Tint=(%.2f,%.2f,%.2f)"),
           *GetName(), *DestNDI->GetName(), *PlyFilePath.FilePath, Splats.Num(), GlobalTint.R, GlobalTint.G,
           GlobalTint.B);

    return true;
}

// Niagara Function Declarations

void UGaussianSplatNiagaraDataInterface::GetFunctions(TArray<FNiagaraFunctionSignature> &OutFunctions)
{
    // GetSplatCount
    {
        FNiagaraFunctionSignature Sig;
        Sig.Name = *GetSplatCountFunctionName;
        Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("GaussianSplatNDI")));
        Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));
        Sig.bMemberFunction = true;
        Sig.bRequiresContext = false;
        OutFunctions.Add(Sig);
    }

    // GetSplatPosition
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

// CPU VM Function Binding & Implementations

void UGaussianSplatNiagaraDataInterface::GetVMExternalFunction(const FVMExternalFunctionBindingInfo &BindingInfo,
                                                               void *InstanceData, FVMExternalFunction &OutFunc)
{
    if (BindingInfo.Name == *GetSplatCountFunctionName)
        NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatCount)::Bind(this, OutFunc);
    else if (BindingInfo.Name == *GetPositionFunctionName)
        NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatPosition)::Bind(this, OutFunc);
    else if (BindingInfo.Name == *GetScaleFunctionName)
        NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatScale)::Bind(this, OutFunc);
    else if (BindingInfo.Name == *GetOrientationFunctionName)
        NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatOrientation)::Bind(this, OutFunc);
    else if (BindingInfo.Name == *GetOpacityFunctionName)
        NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatOpacity)::Bind(this, OutFunc);
    else if (BindingInfo.Name == *GetColorFunctionName)
        NDI_FUNC_BINDER(UGaussianSplatNiagaraDataInterface, GetSplatColor)::Bind(this, OutFunc);
}

void UGaussianSplatNiagaraDataInterface::GetSplatCount(FVectorVMExternalFunctionContext &Context) const
{
    FNDIOutputParam<int32> OutCount(Context);
    const int32 Count = Splats.Num();
    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
        OutCount.SetAndAdvance(Count);
}

void UGaussianSplatNiagaraDataInterface::GetSplatPosition(FVectorVMExternalFunctionContext &Context) const
{
    FNDIInputParam<int32> IndexParam(Context);
    FNDIOutputParam<float> OutPosX(Context);
    FNDIOutputParam<float> OutPosY(Context);
    FNDIOutputParam<float> OutPosZ(Context);

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        const int32 Index = IndexParam.GetAndAdvance();
        if (Splats.IsValidIndex(Index))
        {
            const FGaussianSplatData &S = Splats[Index];
            OutPosX.SetAndAdvance(S.Position.X);
            OutPosY.SetAndAdvance(S.Position.Y);
            OutPosZ.SetAndAdvance(S.Position.Z);
        }
        else
        {
            OutPosX.SetAndAdvance(0.0f);
            OutPosY.SetAndAdvance(0.0f);
            OutPosZ.SetAndAdvance(0.0f);
        }
    }
}

void UGaussianSplatNiagaraDataInterface::GetSplatScale(FVectorVMExternalFunctionContext &Context) const
{
    FNDIInputParam<int32> IndexParam(Context);
    FNDIOutputParam<float> OutX(Context);
    FNDIOutputParam<float> OutY(Context);
    FNDIOutputParam<float> OutZ(Context);

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        const int32 Index = IndexParam.GetAndAdvance();
        if (Splats.IsValidIndex(Index))
        {
            const FGaussianSplatData &S = Splats[Index];
            OutX.SetAndAdvance(S.Scale.X);
            OutY.SetAndAdvance(S.Scale.Y);
            OutZ.SetAndAdvance(S.Scale.Z);
        }
        else
        {
            OutX.SetAndAdvance(1.0f);
            OutY.SetAndAdvance(1.0f);
            OutZ.SetAndAdvance(1.0f);
        }
    }
}

void UGaussianSplatNiagaraDataInterface::GetSplatOrientation(FVectorVMExternalFunctionContext &Context) const
{
    FNDIInputParam<int32> IndexParam(Context);
    FNDIOutputParam<float> OutX(Context);
    FNDIOutputParam<float> OutY(Context);
    FNDIOutputParam<float> OutZ(Context);
    FNDIOutputParam<float> OutW(Context);

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        const int32 Index = IndexParam.GetAndAdvance();
        if (Splats.IsValidIndex(Index))
        {
            const FGaussianSplatData &S = Splats[Index];
            OutX.SetAndAdvance(S.Orientation.X);
            OutY.SetAndAdvance(S.Orientation.Y);
            OutZ.SetAndAdvance(S.Orientation.Z);
            OutW.SetAndAdvance(S.Orientation.W);
        }
        else
        {
            OutX.SetAndAdvance(0.0f);
            OutY.SetAndAdvance(0.0f);
            OutZ.SetAndAdvance(0.0f);
            OutW.SetAndAdvance(1.0f);
        }
    }
}

void UGaussianSplatNiagaraDataInterface::GetSplatOpacity(FVectorVMExternalFunctionContext &Context) const
{
    FNDIInputParam<int32> IndexParam(Context);
    FNDIOutputParam<float> OutOpacity(Context);

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        const int32 Index = IndexParam.GetAndAdvance();
        if (Splats.IsValidIndex(Index))
            OutOpacity.SetAndAdvance(Splats[Index].Opacity);
        else
            OutOpacity.SetAndAdvance(0.0f);
    }
}

void UGaussianSplatNiagaraDataInterface::GetSplatColor(FVectorVMExternalFunctionContext &Context) const
{
    FNDIInputParam<int32> IndexParam(Context);
    FNDIOutputParam<float> OutR(Context);
    FNDIOutputParam<float> OutG(Context);
    FNDIOutputParam<float> OutB(Context);
    FNDIOutputParam<float> OutA(Context);

    for (int32 i = 0; i < Context.GetNumInstances(); ++i)
    {
        const int32 Index = IndexParam.GetAndAdvance();
        if (Splats.IsValidIndex(Index))
        {
            const FGaussianSplatData &S = Splats[Index];
            FLinearColor Color = FGaussianSplatData::SHToColor(S.ZeroOrderHarmonicsCoefficients);
            Color *= GlobalTint;
            OutR.SetAndAdvance(Color.R);
            OutG.SetAndAdvance(Color.G);
            OutB.SetAndAdvance(Color.B);
            OutA.SetAndAdvance(S.Opacity);
        }
        else
        {
            OutR.SetAndAdvance(0.0f);
            OutG.SetAndAdvance(0.0f);
            OutB.SetAndAdvance(0.0f);
            OutA.SetAndAdvance(0.0f);
        }
    }
}

// Shader Parameters Binding

void UGaussianSplatNiagaraDataInterface::BuildShaderParameters(
    FNiagaraShaderParametersBuilder &ShaderParametersBuilder) const
{
    ShaderParametersBuilder.AddNestedStruct<FGaussianSplatShaderParameters>();
}

void UGaussianSplatNiagaraDataInterface::SetShaderParameters(
    const FNiagaraDataInterfaceSetShaderParametersContext &Context) const
{
    FGaussianSplatShaderParameters *ShaderParameters =
        Context.GetParameterNestedStruct<FGaussianSplatShaderParameters>();
    if (!ShaderParameters)
        return;

    FNDIGaussianSplatProxy &DIProxy = Context.GetProxy<FNDIGaussianSplatProxy>();

    // Lazily create fallback buffer on render thread — guaranteed valid after this
    if (!DIProxy.FallbackBuffer.IsValid())
    {
        FRHICommandListImmediate &RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
        const uint32 BytesPerElement = sizeof(FVector4f);
        FRHIResourceCreateInfo CreateInfo(TEXT("GSplat_Fallback"));
        DIProxy.FallbackBuffer.Buffer =
            RHICmdList.CreateVertexBuffer(BytesPerElement, BUF_ShaderResource | BUF_Dynamic, CreateInfo);
        DIProxy.FallbackBuffer.SRV =
            RHICmdList.CreateShaderResourceView(DIProxy.FallbackBuffer.Buffer, BytesPerElement, PF_A32B32G32R32F);
        // Zero it out
        void *Mapped = RHICmdList.LockBuffer(DIProxy.FallbackBuffer.Buffer, 0, BytesPerElement, RLM_WriteOnly);
        if (Mapped)
        {
            FMemory::Memzero(Mapped, BytesPerElement);
            RHICmdList.UnlockBuffer(DIProxy.FallbackBuffer.Buffer);
        }
    }

    const FNiagaraSystemInstanceID InstanceID = Context.GetSystemInstanceID();
    FGaussianSplatInstanceData_RT *InstanceData = DIProxy.SystemInstancesToData_RT.Find(InstanceID);

    const bool bReady = InstanceData && InstanceData->AreBuffersValid() && InstanceData->SplatsCount > 0;

    // ALWAYS bind valid SRVs
    ShaderParameters->SplatsCount = bReady ? InstanceData->SplatsCount : 0;
    ShaderParameters->GlobalTint = bReady ? InstanceData->GlobalTint : FVector3f::OneVector;
    ShaderParameters->Positions = bReady ? InstanceData->PositionsBuffer.SRV : DIProxy.FallbackBuffer.SRV;
    ShaderParameters->Scales = bReady ? InstanceData->ScalesBuffer.SRV : DIProxy.FallbackBuffer.SRV;
    ShaderParameters->Orientations = bReady ? InstanceData->OrientationsBuffer.SRV : DIProxy.FallbackBuffer.SRV;
    ShaderParameters->SHZeroCoeffsAndOpacity =
        bReady ? InstanceData->SHZeroCoeffsAndOpacityBuffer.SRV : DIProxy.FallbackBuffer.SRV;
}

void UGaussianSplatNiagaraDataInterface::DestroyPerInstanceData(void *PerInstanceData,
                                                                FNiagaraSystemInstance *SystemInstance)
{
    FNDIGaussianSplatProxy *RT_Proxy = GetProxyAs<FNDIGaussianSplatProxy>();
    const FNiagaraSystemInstanceID InstanceID = SystemInstance->GetId();

    UE_LOG(LogGaussianSplat, Log, TEXT("[DestroyPerInstanceData] %s | Removing RT instance data"), *GetName());

    ENQUEUE_RENDER_COMMAND(DestroyGaussianSplatInstance)(
        [RT_Proxy, InstanceID](FRHICommandListImmediate &RHICmdList)
        {
            FGaussianSplatInstanceData_RT *Data = RT_Proxy->SystemInstancesToData_RT.Find(InstanceID);
            if (Data)
                Data->ReleaseBuffers();
            RT_Proxy->SystemInstancesToData_RT.Remove(InstanceID);
            UE_LOG(LogTemp, Log, TEXT("[DestroyPerInstanceData RT] Instance removed"));
        });
}

bool UGaussianSplatNiagaraDataInterface::InitPerInstanceData(void *PerInstanceData,
                                                             FNiagaraSystemInstance *SystemInstance)
{
    if (Splats.Num() == 0 && !PlyFilePath.FilePath.IsEmpty())
    {
        UE_LOG(LogGaussianSplat, Warning, TEXT("[InitPerInstanceData] %s | Splats empty, loading from '%s'"),
               *GetName(), *PlyFilePath.FilePath);
        LoadFromPLYFile(PlyFilePath.FilePath);
    }

    FNDIGaussianSplatProxy *RT_Proxy = GetProxyAs<FNDIGaussianSplatProxy>();
    const FNiagaraSystemInstanceID InstanceID = SystemInstance->GetId();
    const FVector3f Tint(GlobalTint.R, GlobalTint.G, GlobalTint.B);
    TArray<FGaussianSplatData> SplatsCopy = Splats; // safe GT-side copy

    UE_LOG(LogGaussianSplat, Warning, TEXT("[InitPerInstanceData] %s | NumSplats=%d — enqueuing GPU init"), *GetName(),
           SplatsCopy.Num());

    ENQUEUE_RENDER_COMMAND(InitGaussianSplatInstance)(
        [RT_Proxy, SplatsCopy = MoveTemp(SplatsCopy), InstanceID, Tint](FRHICommandListImmediate &RHICmdList)
        {
            UE_LOG(LogTemp, Warning, TEXT("[InitPerInstanceData RT] NumSplats=%d"), SplatsCopy.Num());

            FGaussianSplatInstanceData_RT &InstanceData = RT_Proxy->SystemInstancesToData_RT.Add(InstanceID);
            InstanceData.GlobalTint = Tint;

            if (SplatsCopy.Num() > 0)
                RT_Proxy->InitializeAndUpload(RHICmdList, InstanceData, SplatsCopy);
            else
                RT_Proxy->CreateFallbackBuffers(RHICmdList, InstanceData);
        });

    // CRITICAL: block game thread until render command has fully executed.
    // Guarantees that SystemInstancesToData_RT has a valid entry with non-null
    // SRVs before SetShaderParameters can ever be called for this instance.
    FlushRenderingCommands();

    UE_LOG(LogGaussianSplat, Warning, TEXT("[InitPerInstanceData] %s | Flush complete — buffers guaranteed valid"),
           *GetName());
    // set splat count user parameter
    FNiagaraVariable SplatCountVar(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.SplatCount"));

    SystemInstance->GetOverrideParameters()->SetParameterValue<int32>(Splats.Num(), SplatCountVar, true);

    return true;
}

// HLSL Code Generation

void UGaussianSplatNiagaraDataInterface::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo &ParamInfo,
                                                                    FString &OutHLSL)
{
    Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

    OutHLSL.Appendf(TEXT("int %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *SplatsCountParamName);
    OutHLSL.Appendf(TEXT("float3 %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *GlobalTintParamName);
    OutHLSL.Appendf(TEXT("Buffer<float4> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *PositionsBufferName);
    OutHLSL.Appendf(TEXT("Buffer<float4> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *ScalesBufferName);
    OutHLSL.Appendf(TEXT("Buffer<float4> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *OrientationsBufferName);
    OutHLSL.Appendf(TEXT("Buffer<float4> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *SHZeroCoeffsBufferName);
}

bool UGaussianSplatNiagaraDataInterface::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo &ParamInfo,
                                                         const FNiagaraDataInterfaceGeneratedFunction &FunctionInfo,
                                                         int FunctionInstanceIndex, FString &OutHLSL)
{
    if (Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL))
        return true;

    // GetSplatCount
    if (FunctionInfo.DefinitionName == *GetSplatCountFunctionName)
    {
        static const TCHAR *FormatHLSL = TEXT(R"(
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
        static const TCHAR *FormatHLSL = TEXT(R"(
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
        static const TCHAR *FormatHLSL = TEXT(R"(
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
        static const TCHAR *FormatHLSL = TEXT(R"(
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
        static const TCHAR *FormatHLSL = TEXT(R"(
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

    // GetSplatColor — SH-to-color conversion happens on the GPU
    if (FunctionInfo.DefinitionName == *GetColorFunctionName)
    {
        static const TCHAR *FormatHLSL = TEXT(R"(
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

// void UGaussianSplatNiagaraDataInterface::MarkRenderDataDirty()
//{
//	const bool bWasDirty = bGPUDataDirty;
//	bGPUDataDirty = true;
//	UE_LOG(LogGaussianSplat, Verbose, TEXT("[MarkRenderDataDirty] %s | WasDirty=%d -> Now dirty"), *GetName(),
//bWasDirty);
// }
//
// void UGaussianSplatNiagaraDataInterface::PrepareGPUData()
//{
//	FNDIGaussianSplatProxy* SplatProxy = static_cast<FNDIGaussianSplatProxy*>(Proxy.Get());
//	if (!SplatProxy)
//	{
//		UE_LOG(LogGaussianSplat, Error, TEXT("[PrepareGPUData] %s | Proxy is null! Cannot proceed."), *GetName());
//		return;
//	}
//
//	const int32 Count = Splats.Num();
//	const bool bBuffersValid = SplatProxy->AreBuffersValid();
//	const bool bCountMismatch = SplatProxy->SplatsCount != Count;
//	const bool bProxyNeedsInit = !bBuffersValid || bCountMismatch;
//
//	UE_LOG(LogGaussianSplat, Log, TEXT("[PrepareGPUData] %s | Splats=%d | Dirty=%d | BuffersValid=%d | ProxyCount=%d |
//CountMismatch=%d | NeedsInit=%d | Proxy=%p"), 		*GetName(), Count, bGPUDataDirty, bBuffersValid,
//SplatProxy->SplatsCount, bCountMismatch, bProxyNeedsInit, SplatProxy);
//
//	if (bGPUDataDirty || bProxyNeedsInit)
//	{
//		if (Count > 0)
//		{
//			UE_LOG(LogGaussianSplat, Log, TEXT("[PrepareGPUData] %s | Uploading %d splats to GPU (Dirty=%d,
//NeedsInit=%d)"), 				*GetName(), Count, bGPUDataDirty, bProxyNeedsInit); 			SplatProxy->InitializeBuffers(Count);
//			SplatProxy->UploadDataToGPU(Splats);
//			SplatProxy->SplatsCount = Count;
//			SplatProxy->GlobalTint = FVector3f(GlobalTint.R, GlobalTint.G, GlobalTint.B);
//			UE_LOG(LogGaussianSplat, Log, TEXT("[PrepareGPUData] %s | Upload complete. ProxyCount=%d, ProxyValid=%d"),
//				*GetName(), SplatProxy->SplatsCount, SplatProxy->AreBuffersValid());
//		}
//		else
//		{
//			UE_LOG(LogGaussianSplat, Log, TEXT("[PrepareGPUData] %s | No splats (Count=0), creating fallback buffers"),
//*GetName()); 			SplatProxy->EnsureFallbackBuffers();
//		}
//
//		bGPUDataDirty = false;
//		UE_LOG(LogGaussianSplat, Log, TEXT("[PrepareGPUData] %s | Cleared dirty flag"), *GetName());
//	}
//	else
//	{
//		UE_LOG(LogGaussianSplat, Log, TEXT("[PrepareGPUData] %s | SKIPPED: not dirty and proxy up to date"),
//*GetName());
//	}
// }

#undef LOCTEXT_NAMESPACE
