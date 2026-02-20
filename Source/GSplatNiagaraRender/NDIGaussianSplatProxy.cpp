#include "NDIGaussianSplatProxy.h"
#include "GaussianSplatData.h"
#include "RHICommandList.h"
#include "RenderingThread.h"

FNDIGaussianSplatProxy::FNDIGaussianSplatProxy() {}

FNDIGaussianSplatProxy::~FNDIGaussianSplatProxy()
{
    FallbackBuffer.Release();
    for (auto &Pair : SystemInstancesToData_RT)
        Pair.Value.ReleaseBuffers();
    SystemInstancesToData_RT.Empty();
}

void FNDIGaussianSplatProxy::CreateBuffer(FRHICommandListImmediate &RHICmdList, FGaussianSplatBuffer &OutBuffer,
                                          uint32 NumElements, uint32 BytesPerElement, const TCHAR *DebugName)
{
    OutBuffer.NumElements = NumElements;
    const uint32 BufferSize = NumElements * BytesPerElement;
    FRHIResourceCreateInfo CreateInfo(DebugName);
    OutBuffer.Buffer = RHICmdList.CreateVertexBuffer(BufferSize, BUF_ShaderResource | BUF_Dynamic, CreateInfo);
    OutBuffer.SRV = RHICmdList.CreateShaderResourceView(OutBuffer.Buffer, BytesPerElement, PF_A32B32G32R32F);
}

void FNDIGaussianSplatProxy::InitializeAndUpload(FRHICommandListImmediate &RHICmdList,
                                                 FGaussianSplatInstanceData_RT &InstanceData,
                                                 const TArray<FGaussianSplatData> &SplatsData)
{
    check(IsInRenderingThread());
    const int32 NumSplats = SplatsData.Num();

    if (NumSplats <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Proxy::InitializeAndUpload] NumSplats=0, creating fallback"));
        CreateFallbackBuffers(RHICmdList, InstanceData);
        return;
    }

    if (InstanceData.AreBuffersValid())
        InstanceData.ReleaseBuffers();

    const uint32 BytesPerElement = sizeof(FVector4f);
    CreateBuffer(RHICmdList, InstanceData.PositionsBuffer, NumSplats, BytesPerElement, TEXT("GSplat_Positions"));
    CreateBuffer(RHICmdList, InstanceData.ScalesBuffer, NumSplats, BytesPerElement, TEXT("GSplat_Scales"));
    CreateBuffer(RHICmdList, InstanceData.OrientationsBuffer, NumSplats, BytesPerElement, TEXT("GSplat_Orientations"));
    CreateBuffer(RHICmdList, InstanceData.SHZeroCoeffsAndOpacityBuffer, NumSplats, BytesPerElement,
                 TEXT("GSplat_SHOpacity"));
    InstanceData.SplatsCount = NumSplats;

    // Pack into SoA float4 arrays
    TArray<FVector4f> Positions, Scales, Orientations, SHOpacity;
    Positions.SetNum(NumSplats);
    Scales.SetNum(NumSplats);
    Orientations.SetNum(NumSplats);
    SHOpacity.SetNum(NumSplats);

    for (int32 i = 0; i < NumSplats; ++i)
    {
        const FGaussianSplatData &S = SplatsData[i];
        Positions[i] = FVector4f(S.Position.X, S.Position.Y, S.Position.Z, 0.f);
        Scales[i] = FVector4f(S.Scale.X, S.Scale.Y, S.Scale.Z, 0.f);
        Orientations[i] = FVector4f(S.Orientation.X, S.Orientation.Y, S.Orientation.Z, S.Orientation.W);
        SHOpacity[i] = FVector4f(S.ZeroOrderHarmonicsCoefficients.X, S.ZeroOrderHarmonicsCoefficients.Y,
                                 S.ZeroOrderHarmonicsCoefficients.Z, S.Opacity);
    }

    const uint32 DataSize = NumSplats * BytesPerElement;
    auto Upload = [&RHICmdList, DataSize](const FGaussianSplatBuffer &Buf, const void *Data, const TCHAR *Name)
    {
        if (!Buf.IsValid() || !Data)
            return;
        void *Mapped = RHICmdList.LockBuffer(Buf.Buffer, 0, DataSize, RLM_WriteOnly);
        if (Mapped)
        {
            FMemory::Memcpy(Mapped, Data, DataSize);
            RHICmdList.UnlockBuffer(Buf.Buffer);
        }
        UE_LOG(LogTemp, Warning, TEXT("[Proxy::InitializeAndUpload] %s | %d bytes written"), Name, DataSize);
    };

    Upload(InstanceData.PositionsBuffer, Positions.GetData(), TEXT("Positions"));
    Upload(InstanceData.ScalesBuffer, Scales.GetData(), TEXT("Scales"));
    Upload(InstanceData.OrientationsBuffer, Orientations.GetData(), TEXT("Orientations"));
    Upload(InstanceData.SHZeroCoeffsAndOpacityBuffer, SHOpacity.GetData(), TEXT("SHOpacity"));

    UE_LOG(LogTemp, Warning, TEXT("[Proxy::InitializeAndUpload] COMPLETE | %d splats | Valid=%d"), NumSplats,
           InstanceData.AreBuffersValid());
}

void FNDIGaussianSplatProxy::CreateFallbackBuffers(FRHICommandListImmediate &RHICmdList,
                                                   FGaussianSplatInstanceData_RT &InstanceData)
{
    check(IsInRenderingThread());
    if (InstanceData.AreBuffersValid())
        return;

    const uint32 BytesPerElement = sizeof(FVector4f);
    CreateBuffer(RHICmdList, InstanceData.PositionsBuffer, 1, BytesPerElement, TEXT("GSplat_Fallback_Pos"));
    CreateBuffer(RHICmdList, InstanceData.ScalesBuffer, 1, BytesPerElement, TEXT("GSplat_Fallback_Scl"));
    CreateBuffer(RHICmdList, InstanceData.OrientationsBuffer, 1, BytesPerElement, TEXT("GSplat_Fallback_Ori"));
    CreateBuffer(RHICmdList, InstanceData.SHZeroCoeffsAndOpacityBuffer, 1, BytesPerElement, TEXT("GSplat_Fallback_SH"));
    // SplatsCount stays 0 — shader will read nothing
    UE_LOG(LogTemp, Warning, TEXT("[Proxy::CreateFallbackBuffers] Done | Valid=%d"), InstanceData.AreBuffersValid());
}
