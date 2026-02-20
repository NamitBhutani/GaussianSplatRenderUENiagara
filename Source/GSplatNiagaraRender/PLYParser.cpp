#include "PLYParser.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"

FPLYParser::FPLYParser()
    : Format(EPLYFormat::Unknown), VertexCount(0), PropIdx_X(-1), PropIdx_Y(-1), PropIdx_Z(-1), PropIdx_NX(-1),
      PropIdx_NY(-1), PropIdx_NZ(-1), PropIdx_FDC0(-1), PropIdx_FDC1(-1), PropIdx_FDC2(-1), PropIdx_Opacity(-1),
      PropIdx_Scale0(-1), PropIdx_Scale1(-1), PropIdx_Scale2(-1), PropIdx_Rot0(-1), PropIdx_Rot1(-1), PropIdx_Rot2(-1),
      PropIdx_Rot3(-1)
{
}

FPLYParser::~FPLYParser() {}

bool FPLYParser::ParseFile(const FString &FilePath, TArray<FGaussianSplatData> &OutSplats)
{
    OutSplats.Empty();
    ErrorMessage.Empty();

    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*FilePath))
    {
        ErrorMessage = FString::Printf(TEXT("File not found: %s"), *FilePath);
        return false;
    }

    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        ErrorMessage = FString::Printf(TEXT("Failed to load file: %s"), *FilePath);
        return false;
    }

    FString FileContent;
    FFileHelper::BufferToString(FileContent, FileData.GetData(), FileData.Num());

    TArray<FString> Lines;
    FileContent.ParseIntoArrayLines(Lines);

    if (Lines.Num() == 0)
    {
        ErrorMessage = TEXT("Empty file");
        return false;
    }

    // Check PLY magic number
    if (!Lines[0].TrimStartAndEnd().Equals(TEXT("ply"), ESearchCase::IgnoreCase))
    {
        ErrorMessage = TEXT("Invalid PLY file: missing 'ply' header");
        return false;
    }

    // Parse header
    int32 HeaderEndLine = 0;
    if (!ParseHeader(Lines, HeaderEndLine))
    {
        return false;
    }

    if (Format == EPLYFormat::ASCII)
    {
        return ParseASCIIData(Lines, HeaderEndLine + 1, OutSplats);
    }
    else if (Format == EPLYFormat::BinaryLittleEndian || Format == EPLYFormat::BinaryBigEndian)
    {
        int32 HeaderByteOffset = 0;
        for (int32 i = 0; i <= HeaderEndLine; ++i)
        {
            HeaderByteOffset += Lines[i].Len() + 1;
        }

        FString TestString;
        int32 ActualOffset = FileContent.Find(TEXT("end_header"));
        if (ActualOffset != INDEX_NONE)
        {
            while (ActualOffset < FileData.Num() && FileData[ActualOffset] != '\n')
            {
                ActualOffset++;
            }
            HeaderByteOffset = ActualOffset + 1; // Skip the newline
        }

        return ParseBinaryData(FileData, HeaderByteOffset, OutSplats);
    }

    ErrorMessage = TEXT("Unknown PLY format");
    return false;
}

bool FPLYParser::ParseHeader(const TArray<FString> &Lines, int32 &OutHeaderEndLine)
{
    Properties.Empty();
    VertexCount = 0;
    Format = EPLYFormat::Unknown;

    for (int32 LineIdx = 0; LineIdx < Lines.Num(); ++LineIdx)
    {
        FString Line = Lines[LineIdx].TrimStartAndEnd();

        if (Line.Equals(TEXT("end_header"), ESearchCase::IgnoreCase))
        {
            OutHeaderEndLine = LineIdx;

            // Cache property indices for quick lookup
            PropIdx_X = FindPropertyIndex(TEXT("x"));
            PropIdx_Y = FindPropertyIndex(TEXT("y"));
            PropIdx_Z = FindPropertyIndex(TEXT("z"));
            PropIdx_NX = FindPropertyIndex(TEXT("nx"));
            PropIdx_NY = FindPropertyIndex(TEXT("ny"));
            PropIdx_NZ = FindPropertyIndex(TEXT("nz"));
            PropIdx_FDC0 = FindPropertyIndex(TEXT("f_dc_0"));
            PropIdx_FDC1 = FindPropertyIndex(TEXT("f_dc_1"));
            PropIdx_FDC2 = FindPropertyIndex(TEXT("f_dc_2"));
            PropIdx_Opacity = FindPropertyIndex(TEXT("opacity"));
            PropIdx_Scale0 = FindPropertyIndex(TEXT("scale_0"));
            PropIdx_Scale1 = FindPropertyIndex(TEXT("scale_1"));
            PropIdx_Scale2 = FindPropertyIndex(TEXT("scale_2"));
            PropIdx_Rot0 = FindPropertyIndex(TEXT("rot_0"));
            PropIdx_Rot1 = FindPropertyIndex(TEXT("rot_1"));
            PropIdx_Rot2 = FindPropertyIndex(TEXT("rot_2"));
            PropIdx_Rot3 = FindPropertyIndex(TEXT("rot_3"));

            // Find f_rest properties
            PropIdx_FRest.Empty();
            for (int32 i = 0; i < 45; ++i)
            {
                int32 Idx = FindPropertyIndex(FString::Printf(TEXT("f_rest_%d"), i));
                if (Idx != INDEX_NONE)
                {
                    PropIdx_FRest.Add(Idx);
                }
            }

            // Validate required properties
            if (PropIdx_X == INDEX_NONE || PropIdx_Y == INDEX_NONE || PropIdx_Z == INDEX_NONE)
            {
                ErrorMessage = TEXT("Missing required position properties (x, y, z)");
                return false;
            }

            return true;
        }

        if (Line.StartsWith(TEXT("format"), ESearchCase::IgnoreCase))
        {
            if (Line.Contains(TEXT("ascii")))
            {
                Format = EPLYFormat::ASCII;
            }
            else if (Line.Contains(TEXT("binary_little_endian")))
            {
                Format = EPLYFormat::BinaryLittleEndian;
            }
            else if (Line.Contains(TEXT("binary_big_endian")))
            {
                Format = EPLYFormat::BinaryBigEndian;
            }
        }
        else if (Line.StartsWith(TEXT("element vertex"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Tokens;
            Line.ParseIntoArray(Tokens, TEXT(" "));
            if (Tokens.Num() >= 3)
            {
                VertexCount = FCString::Atoi(*Tokens[2]);
            }
        }
        else if (Line.StartsWith(TEXT("property"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Tokens;
            Line.ParseIntoArray(Tokens, TEXT(" "));

            if (Tokens.Num() >= 3)
            {
                FPLYProperty Prop;

                if (Tokens[1].Equals(TEXT("list"), ESearchCase::IgnoreCase))
                {
                    // List property
                    Prop.bIsList = true;
                    if (Tokens.Num() >= 5)
                    {
                        Prop.ListCountType = Tokens[2];
                        Prop.ListElementType = Tokens[3];
                        Prop.Name = Tokens[4];
                    }
                }
                else
                {
                    Prop.Type = Tokens[1];
                    Prop.Name = Tokens[2];
                    Prop.ByteSize = FPLYProperty::GetTypeByteSize(Prop.Type);
                }

                Properties.Add(Prop);
            }
        }
    }

    ErrorMessage = TEXT("Missing end_header");
    return false;
}

bool FPLYParser::ParseASCIIData(const TArray<FString> &Lines, int32 StartLine, TArray<FGaussianSplatData> &OutSplats)
{
    OutSplats.Reserve(VertexCount);

    int32 NumProperties = Properties.Num();
    TArray<float> PropertyValues;
    PropertyValues.SetNum(NumProperties);

    for (int32 i = 0; i < VertexCount; ++i)
    {
        int32 LineIdx = StartLine + i;
        if (LineIdx >= Lines.Num())
        {
            ErrorMessage = FString::Printf(TEXT("Unexpected end of file at vertex %d"), i);
            return false;
        }

        FString Line = Lines[LineIdx].TrimStartAndEnd();
        TArray<FString> Tokens;
        Line.ParseIntoArray(Tokens, TEXT(" "), true);

        if (Tokens.Num() < NumProperties)
        {
            ErrorMessage = FString::Printf(TEXT("Not enough values at vertex %d (expected %d, got %d)"), i,
                                           NumProperties, Tokens.Num());
            return false;
        }

        for (int32 j = 0; j < NumProperties; ++j)
        {
            PropertyValues[j] = FCString::Atof(*Tokens[j]);
        }

        OutSplats.Add(ExtractSplatData(PropertyValues));
    }

    UE_LOG(LogTemp, Log, TEXT("PLYParser: Loaded %d splats from ASCII PLY file"), OutSplats.Num());
    return true;
}

bool FPLYParser::ParseBinaryData(const TArray<uint8> &FileData, int32 HeaderByteOffset,
                                 TArray<FGaussianSplatData> &OutSplats)
{
    OutSplats.Reserve(VertexCount);

    bool bBigEndian = (Format == EPLYFormat::BinaryBigEndian);
    int32 VertexByteSize = CalculateVertexByteSize();
    int32 NumProperties = Properties.Num();

    TArray<float> PropertyValues;
    PropertyValues.SetNum(NumProperties);

    int32 Offset = HeaderByteOffset;

    for (int32 i = 0; i < VertexCount; ++i)
    {
        if (Offset + VertexByteSize > FileData.Num())
        {
            ErrorMessage = FString::Printf(TEXT("Unexpected end of file at vertex %d"), i);
            return false;
        }

        // Read each property
        for (int32 j = 0; j < NumProperties; ++j)
        {
            const FPLYProperty &Prop = Properties[j];

            if (Prop.bIsList)
            {
                continue;
            }

            if (Prop.Type == TEXT("float") || Prop.Type == TEXT("float32"))
            {
                PropertyValues[j] = ReadFloat(FileData.GetData(), Offset, bBigEndian);
            }
            else if (Prop.Type == TEXT("double") || Prop.Type == TEXT("float64"))
            {
                PropertyValues[j] = static_cast<float>(ReadDouble(FileData.GetData(), Offset, bBigEndian));
            }
            else
            {
                Offset += Prop.ByteSize;
                PropertyValues[j] = 0.0f;
            }
        }

        OutSplats.Add(ExtractSplatData(PropertyValues));
    }

    UE_LOG(LogTemp, Log, TEXT("PLYParser: Loaded %d splats from binary PLY file"), OutSplats.Num());
    return true;
}

float FPLYParser::ReadFloat(const uint8 *Data, int32 &Offset, bool bBigEndian)
{
    float Value;
    if (bBigEndian)
    {
        uint8 Bytes[4] = {Data[Offset + 3], Data[Offset + 2], Data[Offset + 1], Data[Offset]};
        FMemory::Memcpy(&Value, Bytes, 4);
    }
    else
    {
        FMemory::Memcpy(&Value, Data + Offset, 4);
    }
    Offset += 4;
    return Value;
}

double FPLYParser::ReadDouble(const uint8 *Data, int32 &Offset, bool bBigEndian)
{
    double Value;
    if (bBigEndian)
    {
        uint8 Bytes[8];
        for (int32 i = 0; i < 8; ++i)
        {
            Bytes[i] = Data[Offset + 7 - i];
        }
        FMemory::Memcpy(&Value, Bytes, 8);
    }
    else
    {
        FMemory::Memcpy(&Value, Data + Offset, 8);
    }
    Offset += 8;
    return Value;
}

int32 FPLYParser::FindPropertyIndex(const FString &Name) const
{
    for (int32 i = 0; i < Properties.Num(); ++i)
    {
        if (Properties[i].Name.Equals(Name, ESearchCase::IgnoreCase))
        {
            return i;
        }
    }
    return INDEX_NONE;
}

int32 FPLYParser::CalculateVertexByteSize() const
{
    int32 Size = 0;
    for (const FPLYProperty &Prop : Properties)
    {
        if (!Prop.bIsList)
        {
            Size += Prop.ByteSize;
        }
    }
    return Size;
}

FGaussianSplatData FPLYParser::ExtractSplatData(const TArray<float> &PropertyValues)
{
    FGaussianSplatData Splat;

    // Position
    float X = PropIdx_X != INDEX_NONE ? PropertyValues[PropIdx_X] : 0.0f;
    float Y = PropIdx_Y != INDEX_NONE ? PropertyValues[PropIdx_Y] : 0.0f;
    float Z = PropIdx_Z != INDEX_NONE ? PropertyValues[PropIdx_Z] : 0.0f;
    Splat.Position = FGaussianSplatData::ConvertPositionToUnreal(X, Y, Z);

    if (PropIdx_NX != INDEX_NONE && PropIdx_NY != INDEX_NONE && PropIdx_NZ != INDEX_NONE)
    {
        Splat.Normal = FVector3f(PropertyValues[PropIdx_NX], PropertyValues[PropIdx_NY], PropertyValues[PropIdx_NZ]);
    }

    // Scale
    if (PropIdx_Scale0 != INDEX_NONE && PropIdx_Scale1 != INDEX_NONE && PropIdx_Scale2 != INDEX_NONE)
    {
        Splat.Scale = FGaussianSplatData::ConvertScaleToUnreal(
            PropertyValues[PropIdx_Scale0], PropertyValues[PropIdx_Scale1], PropertyValues[PropIdx_Scale2]);
    }

    if (PropIdx_Rot0 != INDEX_NONE && PropIdx_Rot1 != INDEX_NONE && PropIdx_Rot2 != INDEX_NONE &&
        PropIdx_Rot3 != INDEX_NONE)
    {
        Splat.Orientation = FGaussianSplatData::ConvertOrientationToUnreal(PropertyValues[PropIdx_Rot0], // W
                                                                           PropertyValues[PropIdx_Rot1], // X
                                                                           PropertyValues[PropIdx_Rot2], // Y
                                                                           PropertyValues[PropIdx_Rot3]  // Z
        );
    }

    // Opacity
    if (PropIdx_Opacity != INDEX_NONE)
    {
        Splat.Opacity = FGaussianSplatData::ConvertOpacityToUnreal(PropertyValues[PropIdx_Opacity]);
    }

    if (PropIdx_FDC0 != INDEX_NONE && PropIdx_FDC1 != INDEX_NONE && PropIdx_FDC2 != INDEX_NONE)
    {
        Splat.ZeroOrderHarmonicsCoefficients =
            FVector3f(PropertyValues[PropIdx_FDC0], PropertyValues[PropIdx_FDC1], PropertyValues[PropIdx_FDC2]);
    }

    // Higher order spherical harmonics (optional, for view-dependent color)
    if (PropIdx_FRest.Num() > 0)
    {
        Splat.HighOrderHarmonicsCoefficients.Reserve(PropIdx_FRest.Num() / 3);
        for (int32 i = 0; i + 2 < PropIdx_FRest.Num(); i += 3)
        {
            Splat.HighOrderHarmonicsCoefficients.Add(FVector3f(PropertyValues[PropIdx_FRest[i]],
                                                               PropertyValues[PropIdx_FRest[i + 1]],
                                                               PropertyValues[PropIdx_FRest[i + 2]]));
        }
    }

    return Splat;
}
