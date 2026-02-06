#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "VectorVM.h"
#include "GaussianSplatData.h"
#include "NDIGaussianSplatProxy.h" 
#include "GaussianSplatNiagaraDataInterface.generated.h"

// Forward declaration for the Tick function
class FNiagaraSystemInstance;

BEGIN_SHADER_PARAMETER_STRUCT(FGaussianSplatShaderParameters, )
    SHADER_PARAMETER(int, SplatsCount)
    SHADER_PARAMETER(FVector3f, GlobalTint)
    SHADER_PARAMETER_SRV(Buffer<float4>, Positions)
    SHADER_PARAMETER_SRV(Buffer<float4>, Scales)
    SHADER_PARAMETER_SRV(Buffer<float4>, Orientations)
    SHADER_PARAMETER_SRV(Buffer<float4>, SHZeroCoeffsAndOpacity)
END_SHADER_PARAMETER_STRUCT()

// Thread-safe shared pointer definition
typedef TSharedPtr<TArray<FGaussianSplatData>, ESPMode::ThreadSafe> FSplatDataPtr;

UCLASS(EditInlineNew, Category = "Gaussian Splat", meta = (DisplayName = "Gaussian Splat NDI"))
class GSPLATNIAGARARENDER_API UGaussianSplatNiagaraDataInterface : public UNiagaraDataInterface
{
    GENERATED_UCLASS_BODY()

public:
    // SHARED POINTER: Replaces the TArray for thread-safe, copy-free access
    FSplatDataPtr SplatData;

    // Helper property to see count in Editor (Splats are no longer a UPROPERTY to avoid copy)
    UPROPERTY(VisibleAnywhere, Category = "Gaussian Splat")
    int32 CurrentSplatCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    FLinearColor GlobalTint;

    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    bool LoadFromPLYFile(const FString& FilePath);

    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    int32 GetSplatCount() const;

    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    void ClearSplats();

    // UNiagaraDataInterface overrides
    virtual void PostInitProperties() override;
    virtual void PostLoad() override;
    virtual void BeginDestroy() override;

    virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;

    virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
    virtual bool Equals(const UNiagaraDataInterface* Other) const override;

    virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
    virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;

    // Force GPU-only execution
    virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override;

    // GPU support
    virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
    virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;
    virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
    virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
    
    // CPU VM functions (stubs required for validation, though we run on GPU)
    void GetSplatCount(FVectorVMExternalFunctionContext& Context) const;
    void GetSplatPosition(FVectorVMExternalFunctionContext& Context) const;
    void GetSplatScale(FVectorVMExternalFunctionContext& Context) const;
    void GetSplatOrientation(FVectorVMExternalFunctionContext& Context) const;
    void GetSplatOpacity(FVectorVMExternalFunctionContext& Context) const;
    void GetSplatColor(FVectorVMExternalFunctionContext& Context) const;

    void MarkRenderDataDirty();
    void PrepareGPUData();

private:
    static const FString GetSplatCountFunctionName;
    static const FString GetPositionFunctionName;
    static const FString GetScaleFunctionName;
    static const FString GetOrientationFunctionName;
    static const FString GetOpacityFunctionName;
    static const FString GetColorFunctionName;

    static const FString SplatsCountParamName;
    static const FString GlobalTintParamName;
    static const FString PositionsBufferName;
    static const FString ScalesBufferName;
    static const FString OrientationsBufferName;
    static const FString SHZeroCoeffsBufferName;

    bool bGPUDataDirty;
};