#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "GaussianSplatNiagaraDataInterface.h"
#include "GaussianSplatActor.generated.h"

UCLASS(BlueprintType, Blueprintable)
class GSPLATNIAGARARENDER_API AGaussianSplatActor : public AActor
{
    GENERATED_BODY()

public:
    AGaussianSplatActor();

    virtual void PostInitializeComponents() override;
    
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

   
    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    bool LoadPLYFile(const FString& FilePath);

    
    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    void ClearSplats();

  
    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    int32 GetSplatCount() const;

   
    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    void SetGlobalTint(FLinearColor NewTint);

    UFUNCTION(BlueprintCallable, Category = "Gaussian Splat")
    void RefreshNiagaraSystem();

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    TObjectPtr<UNiagaraSystem> NiagaraSystemAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    FString AutoLoadPLYPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    bool bAutoLoadOnBeginPlay;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    FLinearColor GlobalTint;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UNiagaraComponent> NiagaraComponent;

    UPROPERTY()
    TObjectPtr<UGaussianSplatNiagaraDataInterface> SplatDataInterface;

private:
    void SetupNiagaraComponent();
};  
