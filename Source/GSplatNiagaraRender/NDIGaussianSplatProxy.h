#pragma once

#include "CoreMinimal.h"
#include "GaussianSplatData.h"
#include "NiagaraCommon.h"
#include "NiagaraDataInterfaceRW.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RenderResource.h"

struct FGaussianSplatBuffer
{
    FBufferRHIRef Buffer;
    FShaderResourceViewRHIRef SRV;
    uint32 NumElements;

    FGaussianSplatBuffer() : NumElements(0) {}

    void Release()
    {
        Buffer.SafeRelease();
        SRV.SafeRelease();
        NumElements = 0;
    }

    bool IsValid() const
    {
        return Buffer.IsValid() && SRV.IsValid();
    }
};

struct FGaussianSplatInstanceData_RT
{
    FGaussianSplatBuffer PositionsBuffer;
    FGaussianSplatBuffer ScalesBuffer;
    FGaussianSplatBuffer OrientationsBuffer;
    FGaussianSplatBuffer SHZeroCoeffsAndOpacityBuffer;
    int32 SplatsCount = 0;
    FVector3f GlobalTint = FVector3f::OneVector;

    bool AreBuffersValid() const
    {
        return PositionsBuffer.IsValid() && ScalesBuffer.IsValid() && OrientationsBuffer.IsValid() &&
               SHZeroCoeffsAndOpacityBuffer.IsValid();
    }

    void ReleaseBuffers()
    {
        PositionsBuffer.Release();
        ScalesBuffer.Release();
        OrientationsBuffer.Release();
        SHZeroCoeffsAndOpacityBuffer.Release();
        SplatsCount = 0;
    }
};

class FNDIGaussianSplatProxy : public FNiagaraDataInterfaceProxy
{
public:
    FNDIGaussianSplatProxy();
    virtual ~FNDIGaussianSplatProxy();

    virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
    {
        return 0;
    }
    virtual void ConsumePerInstanceDataFromGameThread(void *PerInstanceData,
                                                      const FNiagaraSystemInstanceID &Instance) override
    {
    }

    // Called on the render thread from InitPerInstanceData's enqueued command
    void InitializeAndUpload(FRHICommandListImmediate &RHICmdList, FGaussianSplatInstanceData_RT &InstanceData,
                             const TArray<FGaussianSplatData> &SplatsData);

    // Creates 1 element zeroed buffers so SRVs are never null when no data is available
    void CreateFallbackBuffers(FRHICommandListImmediate &RHICmdList, FGaussianSplatInstanceData_RT &InstanceData);

    // one entry per live NiagaraComponent
    TMap<FNiagaraSystemInstanceID, FGaussianSplatInstanceData_RT> SystemInstancesToData_RT;
    FGaussianSplatBuffer FallbackBuffer;

private:
    void CreateBuffer(FRHICommandListImmediate &RHICmdList, FGaussianSplatBuffer &OutBuffer, uint32 NumElements,
                      uint32 BytesPerElement, const TCHAR *DebugName);
};
