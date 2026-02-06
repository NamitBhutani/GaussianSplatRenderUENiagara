#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterfaceRW.h"
#include "RenderResource.h"
#include "RHI.h"
#include "RHIResources.h"

struct FGaussianSplatData;
class UGaussianSplatNiagaraDataInterface;

struct FGaussianSplatBuffer
{
    FBufferRHIRef Buffer;
    FShaderResourceViewRHIRef SRV;
    uint32 NumElements;

    FGaussianSplatBuffer()
        : NumElements(0)
    {
    }

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


class FNDIGaussianSplatProxy : public FNiagaraDataInterfaceProxy
{
public:
    FNDIGaussianSplatProxy();
    virtual ~FNDIGaussianSplatProxy();

    void InitializeBuffers(int32 NumSplats);

    void ReleaseBuffers();


    void UploadDataToGPU(const TArray<FGaussianSplatData>& SplatsData);

    /** Check if buffers are properly initialized */
    bool AreBuffersValid() const
    {
        return PositionsBuffer.IsValid() 
            && ScalesBuffer.IsValid()
            && OrientationsBuffer.IsValid()
            && SHZeroCoeffsAndOpacityBuffer.IsValid();
    }

    // FNiagaraDataInterfaceProxy interface
    virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }
    virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override {}

public:
    // GPU Buffers  
    FGaussianSplatBuffer PositionsBuffer;
    FGaussianSplatBuffer ScalesBuffer;
    FGaussianSplatBuffer OrientationsBuffer;
    FGaussianSplatBuffer SHZeroCoeffsAndOpacityBuffer;

    // Constants
    int32 SplatsCount;
    FVector3f GlobalTint;

    TWeakObjectPtr<const UGaussianSplatNiagaraDataInterface> SourceDataInterface;

private:
    void CreateBuffer(FGaussianSplatBuffer& OutBuffer, uint32 NumElements, uint32 BytesPerElement, const TCHAR* DebugName);
};
