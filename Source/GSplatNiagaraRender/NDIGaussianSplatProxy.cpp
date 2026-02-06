#include "NDIGaussianSplatProxy.h"
#include "GaussianSplatData.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

FNDIGaussianSplatProxy::FNDIGaussianSplatProxy()
    : SplatsCount(0)
    , GlobalTint(FVector3f::OneVector)
{
}

FNDIGaussianSplatProxy::~FNDIGaussianSplatProxy()
{
    ReleaseBuffers();
}

void FNDIGaussianSplatProxy::InitializeBuffers(int32 NumSplats)
{
    if (NumSplats <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FNDIGaussianSplatProxy::InitializeBuffers: Invalid NumSplats (%d)"), NumSplats);
        return;
    }

    // Release existing buffers if they exist
    if (AreBuffersValid())
    {
        UE_LOG(LogTemp, Log, TEXT("FNDIGaussianSplatProxy: Releasing existing buffers before re-initialization"));
        ReleaseBuffers();
    }

    SplatsCount = NumSplats;

    // Create buffers for each data type (float4 = 16 bytes per element)
    const uint32 BytesPerElement = sizeof(FVector4f);

    CreateBuffer(PositionsBuffer, NumSplats, BytesPerElement, TEXT("GaussianSplat_Positions"));
    CreateBuffer(ScalesBuffer, NumSplats, BytesPerElement, TEXT("GaussianSplat_Scales"));
    CreateBuffer(OrientationsBuffer, NumSplats, BytesPerElement, TEXT("GaussianSplat_Orientations"));
    CreateBuffer(SHZeroCoeffsAndOpacityBuffer, NumSplats, BytesPerElement, TEXT("GaussianSplat_SHAndOpacity"));

    UE_LOG(LogTemp, Log, TEXT("FNDIGaussianSplatProxy: Initialized buffers for %d splats"), NumSplats);
}

void FNDIGaussianSplatProxy::ReleaseBuffers()
{
    ENQUEUE_RENDER_COMMAND(ReleaseGaussianSplatBuffers)(
        [this](FRHICommandListImmediate& RHICmdList)
        {
            PositionsBuffer.Release();
            ScalesBuffer.Release();
            OrientationsBuffer.Release();
            SHZeroCoeffsAndOpacityBuffer.Release();
        }
    );

    SplatsCount = 0;
}

void FNDIGaussianSplatProxy::UploadDataToGPU(const TArray<FGaussianSplatData>& SplatsData)
{
    const int32 NumSplats = SplatsData.Num();
    
    if (NumSplats <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FNDIGaussianSplatProxy::UploadDataToGPU: No splats to upload"));
        return;
    }

    if (!AreBuffersValid())
    {
        UE_LOG(LogTemp, Error, TEXT("FNDIGaussianSplatProxy::UploadDataToGPU: Buffers not initialized!"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("FNDIGaussianSplatProxy: Uploading %d splats to GPU..."), NumSplats);

    // Prepare data arrays for GPU upload (float4 format)
    TArray<FVector4f> PositionsData;
    TArray<FVector4f> ScalesData;
    TArray<FVector4f> OrientationsData;
    TArray<FVector4f> SHAndOpacityData;
    
    PositionsData.SetNum(NumSplats);
    ScalesData.SetNum(NumSplats);
    OrientationsData.SetNum(NumSplats);
    SHAndOpacityData.SetNum(NumSplats);
    
    // Convert each splat to GPU-friendly format
    for (int32 i = 0; i < NumSplats; ++i)
    {
        const FGaussianSplatData& Splat = SplatsData[i];
        
        // Position (XYZ, W unused)
        PositionsData[i] = FVector4f(
            static_cast<float>(Splat.Position.X),
            static_cast<float>(Splat.Position.Y),
            static_cast<float>(Splat.Position.Z),
            0.0f
        );
        
        // Scale (XYZ, W unused)
        ScalesData[i] = FVector4f(
            static_cast<float>(Splat.Scale.X),
            static_cast<float>(Splat.Scale.Y),
            static_cast<float>(Splat.Scale.Z),
            0.0f
        );
        
        // Orientation (XYZW quaternion)
        OrientationsData[i] = FVector4f(
            static_cast<float>(Splat.Orientation.X),
            static_cast<float>(Splat.Orientation.Y),
            static_cast<float>(Splat.Orientation.Z),
            static_cast<float>(Splat.Orientation.W)
        );
        
        // Spherical Harmonics Zero Order + Opacity (RGB + A)
        SHAndOpacityData[i] = FVector4f(
            static_cast<float>(Splat.ZeroOrderHarmonicsCoefficients.X),
            static_cast<float>(Splat.ZeroOrderHarmonicsCoefficients.Y),
            static_cast<float>(Splat.ZeroOrderHarmonicsCoefficients.Z),
            Splat.Opacity
        );
    }
    
    // ===== UPLOAD TO GPU =====
    // Capture data by move to avoid copies
    ENQUEUE_RENDER_COMMAND(UploadGaussianSplatData)(
        [this, 
         PositionsData = MoveTemp(PositionsData), 
         ScalesData = MoveTemp(ScalesData), 
         OrientationsData = MoveTemp(OrientationsData), 
         SHAndOpacityData = MoveTemp(SHAndOpacityData),
         NumSplats]
        (FRHICommandListImmediate& RHICmdList)
        {
            const uint32 DataSize = NumSplats * sizeof(FVector4f);
            
            // Lambda to upload data to a buffer
            auto UploadBuffer = [&RHICmdList](const FGaussianSplatBuffer& Buffer, const void* Data, uint32 Size)
            {
                if (!Buffer.IsValid() || Data == nullptr || Size == 0)
                {
                    return;
                }
                
                void* MappedData = RHICmdList.LockBuffer(Buffer.Buffer, 0, Size, RLM_WriteOnly);
                if (MappedData)
                {
                    FMemory::Memcpy(MappedData, Data, Size);
                    RHICmdList.UnlockBuffer(Buffer.Buffer);
                }
            };
            
            // Upload each buffer
            UploadBuffer(PositionsBuffer, PositionsData.GetData(), DataSize);
            UploadBuffer(ScalesBuffer, ScalesData.GetData(), DataSize);
            UploadBuffer(OrientationsBuffer, OrientationsData.GetData(), DataSize);
            UploadBuffer(SHZeroCoeffsAndOpacityBuffer, SHAndOpacityData.GetData(), DataSize);
        }
    );
    
    UE_LOG(LogTemp, Log, TEXT("FNDIGaussianSplatProxy: ✓ Data upload enqueued to GPU"));
}

void FNDIGaussianSplatProxy::CreateBuffer(FGaussianSplatBuffer& OutBuffer, uint32 NumElements, uint32 BytesPerElement, const TCHAR* DebugName)
{
    OutBuffer.NumElements = NumElements;
    const uint32 BufferSize = NumElements * BytesPerElement;

    ENQUEUE_RENDER_COMMAND(CreateGaussianSplatBuffer)(
        [&OutBuffer, BufferSize, BytesPerElement, DebugName](FRHICommandListImmediate& RHICmdList)
        {
            FRHIResourceCreateInfo CreateInfo(DebugName);
            
            // Create vertex buffer for GPU access
            OutBuffer.Buffer = RHICmdList.CreateVertexBuffer(
                BufferSize,
                BUF_ShaderResource | BUF_Dynamic, // Changed to BUF_Dynamic for updates
                CreateInfo
            );

            // Create shader resource view for reading in shaders
            OutBuffer.SRV = RHICmdList.CreateShaderResourceView(
                OutBuffer.Buffer,
                BytesPerElement,
                PF_A32B32G32R32F // Float4 format
            );
        }
    );

    // Wait for render thread to complete buffer creation
    FlushRenderingCommands();
}
