#include "GaussianSplatActor.h"
#include "NiagaraFunctionLibrary.h"
#include "Misc/Paths.h"

AGaussianSplatActor::AGaussianSplatActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // Create root component
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    // Create Niagara component
    NiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("NiagaraComponent"));
    NiagaraComponent->SetupAttachment(RootComponent);
    
    NiagaraComponent->SetAutoActivate(false);

    bAutoLoadOnBeginPlay = true;  
    GlobalTint = FLinearColor::White;
}

void AGaussianSplatActor::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    
    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════╗"));
    UE_LOG(LogTemp, Warning, TEXT("║  PostInitializeComponents Called!      ║"));
    UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════╝"));
    
    SetupNiagaraComponent();
    
    // Load PLY file BEFORE PIE starts
    if (bAutoLoadOnBeginPlay && !AutoLoadPLYPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("Auto-loading PLY file: %s"), *AutoLoadPLYPath);
        
        if (LoadPLYFile(AutoLoadPLYPath))
        {
            UE_LOG(LogTemp, Warning, TEXT("✓ Successfully loaded %d splats in PostInitializeComponents"), 
                GetSplatCount());
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("⚠ Failed to load PLY file in PostInitializeComponents"));
        }
    }
    else if (!bAutoLoadOnBeginPlay)
    {
        UE_LOG(LogTemp, Warning, TEXT("Auto-load disabled"));
    }
    else if (AutoLoadPLYPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("No PLY path specified"));
    }
    
    UE_LOG(LogTemp, Warning, TEXT("SplatDataInterface has %d splats"), GetSplatCount());
    UE_LOG(LogTemp, Warning, TEXT("═══════════════════════════════════════"));
    UE_LOG(LogTemp, Warning, TEXT(""));
}

void AGaussianSplatActor::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════╗"));
    UE_LOG(LogTemp, Warning, TEXT("║       BeginPlay Called!                ║"));
    UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════╝"));
    
    
    const int32 SplatCount = GetSplatCount();
    UE_LOG(LogTemp, Warning, TEXT("SplatDataInterface has %d splats"), SplatCount);
    
    if (SplatCount > 0)
    {
        // Bind and activate Niagara
        UE_LOG(LogTemp, Warning, TEXT("Binding NDI and activating Niagara..."));
        RefreshNiagaraSystem();
        UE_LOG(LogTemp, Warning, TEXT("✓ Niagara activated with %d splats"), SplatCount);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠ No splats loaded - Niagara not activated"));
    }
    
    UE_LOG(LogTemp, Warning, TEXT("═══════════════════════════════════════"));
    UE_LOG(LogTemp, Warning, TEXT(""));
}

void AGaussianSplatActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

bool AGaussianSplatActor::LoadPLYFile(const FString& FilePath)
{
    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════╗"));
    UE_LOG(LogTemp, Warning, TEXT("║     LoadPLYFile Called!                ║"));
    UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════╝"));
    
    if (!SplatDataInterface)
    {
        UE_LOG(LogTemp, Warning, TEXT("Creating new SplatDataInterface..."));
        SplatDataInterface = NewObject<UGaussianSplatNiagaraDataInterface>(this, NAME_None);
    }

    FString ResolvedPath = FilePath;
    if (FPaths::IsRelative(FilePath))
    {
        ResolvedPath = FPaths::Combine(FPaths::ProjectContentDir(), FilePath);
    }

    UE_LOG(LogTemp, Warning, TEXT("Loading PLY from: %s"), *ResolvedPath);
    
    if (!SplatDataInterface->LoadFromPLYFile(ResolvedPath))
    {
        UE_LOG(LogTemp, Error, TEXT("❌ AGaussianSplatActor: Failed to load PLY file: %s"), *ResolvedPath);
        UE_LOG(LogTemp, Warning, TEXT("═══════════════════════════════════════"));
        return false;
    }

    SplatDataInterface->GlobalTint = GlobalTint;

    UE_LOG(LogTemp, Warning, TEXT("✓ Loaded %d splats from %s"), 
        SplatDataInterface->GetSplatCount(), *ResolvedPath);
    UE_LOG(LogTemp, Warning, TEXT("═══════════════════════════════════════"));
    UE_LOG(LogTemp, Warning, TEXT(""));
    
    return true;
}

void AGaussianSplatActor::ClearSplats()
{
    if (SplatDataInterface)
    {
        SplatDataInterface->ClearSplats();
        RefreshNiagaraSystem();
    }
}

int32 AGaussianSplatActor::GetSplatCount() const
{
    return SplatDataInterface ? SplatDataInterface->GetSplatCount() : 0;
}

void AGaussianSplatActor::SetGlobalTint(FLinearColor NewTint)
{
    GlobalTint = NewTint;

    if (SplatDataInterface)
    {
        SplatDataInterface->GlobalTint = NewTint;
        SplatDataInterface->MarkRenderDataDirty();
    }
}

void AGaussianSplatActor::RefreshNiagaraSystem()
{
    if (!NiagaraComponent || !SplatDataInterface)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ RefreshNiagaraSystem: Component or Interface is NULL!"));
        return;
    }

    UE_LOG(LogTemp, Error, TEXT(""));
    UE_LOG(LogTemp, Error, TEXT("╔════════════════════════════════════════╗"));
    UE_LOG(LogTemp, Error, TEXT("║   RefreshNiagaraSystem Called!         ║"));
    UE_LOG(LogTemp, Error, TEXT("╚════════════════════════════════════════╝"));
    UE_LOG(LogTemp, Error, TEXT("SplatDataInterface->GetSplatCount() = %d"), 
        SplatDataInterface->GetSplatCount());
    
    
    UE_LOG(LogTemp, Error, TEXT("Deactivating system..."));
    if (NiagaraComponent->IsActive())
    {
        NiagaraComponent->DeactivateImmediate();
    }
    
    UE_LOG(LogTemp, Error, TEXT("Clearing old binding..."));
    NiagaraComponent->SetVariableObject(FName(TEXT("User.GaussianSplatData")), nullptr);
    
    UE_LOG(LogTemp, Error, TEXT("Binding NDI with %d splats..."), 
        SplatDataInterface->GetSplatCount());
    NiagaraComponent->SetVariableObject(FName(TEXT("User.GaussianSplatData")), SplatDataInterface);
    
    UE_LOG(LogTemp, Error, TEXT("Activating system..."));
    NiagaraComponent->Activate(true);
    
    UE_LOG(LogTemp, Error, TEXT("✓ System refreshed"));
    UE_LOG(LogTemp, Error, TEXT("═══════════════════════════════════════"));
    UE_LOG(LogTemp, Error, TEXT(""));
}

void AGaussianSplatActor::SetupNiagaraComponent()
{
    if (!NiagaraComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("SetupNiagaraComponent: NiagaraComponent is NULL!"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("SetupNiagaraComponent called"));
    
    // Set the Niagara system asset
    if (NiagaraSystemAsset)
    {
        NiagaraComponent->SetAsset(NiagaraSystemAsset);
        UE_LOG(LogTemp, Warning, TEXT("✓ Set Niagara asset"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠ No NiagaraSystemAsset set"));
    }

    // Create the data interface if it doesn't exist
    if (!SplatDataInterface)
    {
        SplatDataInterface = NewObject<UGaussianSplatNiagaraDataInterface>(this, NAME_None);
        SplatDataInterface->GlobalTint = GlobalTint;
        UE_LOG(LogTemp, Warning, TEXT("✓ Created SplatDataInterface"));
    }
}
