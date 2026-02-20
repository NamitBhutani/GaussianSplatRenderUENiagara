#pragma once

#include "CoreMinimal.h"
#include "GaussianSplatData.h"
#include "GaussianSplatNiagaraDataInterface.generated.h"
#include "NDIGaussianSplatProxy.h"
#include "NiagaraCommon.h"
#include "NiagaraDataInterface.h"
#include "NiagaraShared.h"
#include "VectorVM.h"

BEGIN_SHADER_PARAMETER_STRUCT(FGaussianSplatShaderParameters, )
SHADER_PARAMETER(int, SplatsCount)
SHADER_PARAMETER(FVector3f, GlobalTint)
SHADER_PARAMETER_SRV(Buffer<float4>, Positions)
SHADER_PARAMETER_SRV(Buffer<float4>, Scales)
SHADER_PARAMETER_SRV(Buffer<float4>, Orientations)
SHADER_PARAMETER_SRV(Buffer<float4>, SHZeroCoeffsAndOpacity)
END_SHADER_PARAMETER_STRUCT()

UCLASS(EditInlineNew, Category = "Gaussian Splat", meta = (DisplayName = "Gaussian Splat NDI"))
class GSPLATNIAGARARENDER_API UGaussianSplatNiagaraDataInterface : public UNiagaraDataInterface
{
    GENERATED_UCLASS_BODY()

public:
    TArray<FGaussianSplatData> Splats;

    UPROPERTY(VisibleAnywhere, Category = "Gaussian Splat")
    int32 CurrentSplatCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    FLinearColor GlobalTint;

    UPROPERTY(EditAnywhere, Category = "Source", meta = (FilePathFilter = "ply"))
    FFilePath PlyFilePath;

    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    bool LoadFromPLYFile(const FString &FilePath);

    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    int32 GetSplatCount() const;

    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    void ClearSplats();

    virtual void PostInitProperties() override;
    virtual void PostLoad() override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(struct FPropertyChangedEvent &PropertyChangedEvent) override;
#endif
    virtual void BeginDestroy() override;

    virtual bool CopyToInternal(UNiagaraDataInterface *Destination) const override;
    virtual bool Equals(const UNiagaraDataInterface *Other) const override;
    virtual void GetFunctions(TArray<FNiagaraFunctionSignature> &OutFunctions) override;
    virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo &BindingInfo, void *InstanceData,
                                       FVMExternalFunction &OutFunc) override;
    virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override
    {
        return Target == ENiagaraSimTarget::GPUComputeSim;
    }

    virtual bool InitPerInstanceData(void *PerInstanceData, FNiagaraSystemInstance *SystemInstance) override;
    virtual void DestroyPerInstanceData(void *PerInstanceData, FNiagaraSystemInstance *SystemInstance) override;
    virtual int32 PerInstanceDataSize() const override
    {
        return sizeof(int32);
    }

    virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder &ShaderParametersBuilder) const override;
    virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext &Context) const override;
    virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo &ParamInfo,
                                            FString &OutHLSL) override;
    virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo &ParamInfo,
                                 const FNiagaraDataInterfaceGeneratedFunction &FunctionInfo, int FunctionInstanceIndex,
                                 FString &OutHLSL) override;

    // CPU VM stubs
    void GetSplatCount(FVectorVMExternalFunctionContext &Context) const;
    void GetSplatPosition(FVectorVMExternalFunctionContext &Context) const;
    void GetSplatScale(FVectorVMExternalFunctionContext &Context) const;
    void GetSplatOrientation(FVectorVMExternalFunctionContext &Context) const;
    void GetSplatOpacity(FVectorVMExternalFunctionContext &Context) const;
    void GetSplatColor(FVectorVMExternalFunctionContext &Context) const;

    void MarkRenderDataDirty();

private:
    void LoadPlyFile();

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
