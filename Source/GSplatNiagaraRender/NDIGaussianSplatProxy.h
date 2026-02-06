#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterfaceRW.h"
#include "RenderResource.h"
#include "RHI.h"
#include "RHIResources.h"

struct FGaussianSplatData;
class UGaussianSplatNiagaraDataInterface;

/**
 * GPU buffer wrapper for a single type of splat data
 */
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

/**
 * Niagara Data Interface Proxy for Gaussian Splat data
 * Handles the CPU-to-GPU data transfer for splat rendering
 */
class FNDIGaussianSplatProxy : public FNiagaraDataInterfaceProxy
{
public:
    FNDIGaussianSplatProxy();
    virtual ~FNDIGaussianSplatProxy();

    /** Initialize GPU buffers with the specified number of splats */
    void InitializeBuffers(int32 NumSplats);

    /** Release all GPU buffers */
    void ReleaseBuffers();

    // ===== NEW: Upload data from CPU TArray<FGaussianSplatData> to GPU =====
    /** 
     * Upload Gaussian Splat data from CPU to GPU buffers
     * Converts FGaussianSplatData to GPU-friendly float4 format
     */
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

    // ===== NEW: Keep reference to source NDI for accessing fresh data =====
    TWeakObjectPtr<const UGaussianSplatNiagaraDataInterface> SourceDataInterface;

private:
    /** Create a GPU buffer with SRV */
    void CreateBuffer(FGaussianSplatBuffer& OutBuffer, uint32 NumElements, uint32 BytesPerElement, const TCHAR* DebugName);
};
