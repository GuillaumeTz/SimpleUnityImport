/* Copyright (C) 2021 Guillaume Tazé <guillaume.taze@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/


#include "SimpleUnityAnimImportFactory.h"
#include "SimpleUnityImportModule.h"

#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"
#include "Animation/AnimSequence.h"

#include "Interfaces/IMainFrameModule.h"
#include "PropertyEditorModule.h"
#include "IStructureDetailsView.h"

#include "yaml-cpp/yaml.h"
#include "EditorFramework/AssetImportData.h"


#define LOCTEXT_NAMESPACE "SimpleUnityAnimImportFactory"

//////////////////////////////////////////////////////////////////////////

USimpleUnityAnimImportImportFactory::USimpleUnityAnimImportImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	bCreateNew = false;
	bEditAfterNew = true;
	SupportedClass = UAnimSequence::StaticClass();

	bEditorImport = true;
	bText = true;

	// Give this factory a lower than normal import priority, as CSV and JSON can be commonly used and we'd like to give the other import factories a shot first
	--ImportPriority;

	Formats.Add(TEXT("anim;Unity .anim file"));
}

FText USimpleUnityAnimImportImportFactory::GetDisplayName() const
{
	return LOCTEXT("SimpleUnitAnimImportImportFactoryDescription", "Unity .anim file");
}

bool USimpleUnityAnimImportImportFactory::DoesSupportClass(UClass * Class)
{
	return Class == UAnimSequence::StaticClass();
}

bool USimpleUnityAnimImportImportFactory::FactoryCanImport(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename);

	if (Extension == TEXT("anim"))
	{
		return true;
	}
	return false;
}

void USimpleUnityAnimImportImportFactory::CleanUp()
{
	Super::CleanUp();
	
	bImportAll = false;
}

FName GetBoneNameFromActorPath(std::string ActorPath)
{
	const size_t LastSlashOffset = ActorPath.rfind("/");
	if (LastSlashOffset != std::string::npos)
	{
		ActorPath = ActorPath.substr(LastSlashOffset);
	}
	const size_t ActorOffset = ActorPath.find("actor:");
	if (ActorOffset != std::string::npos)
	{
		ActorPath = ActorPath.substr(ActorOffset + 6);
	}
	return ActorPath.c_str();
}

UObject* USimpleUnityAnimImportImportFactory::FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, Type);

	bOutOperationCanceled = false;
	if (!bImportAll)
	{
		TSharedPtr<SWindow> ParentWindow;
		// Check if the main frame is loaded.  When using the old main frame it may not be.
		if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
		{
			IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
			ParentWindow = MainFrame.GetParentWindow();
		}

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(FText::Format(LOCTEXT("Window title", "Import file {0}"), FText::FromString(CurrentFilename)))
			.SizingRule(ESizingRule::Autosized);

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bShowPropertyMatrixButton = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.ViewIdentifier = NAME_None;
		DetailsViewArgs.bShowCustomFilterOption = false;
		DetailsViewArgs.bShowOptions = false;

		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FStructureDetailsViewArgs StructViewArgs;
		TSharedPtr<FStructOnScope> StructOnScope = MakeShared<FStructOnScope>(FSimpleUnityAnimImportSettings::StaticStruct(), (uint8*)(&AutomatedImportSettings));
		TSharedRef<IStructureDetailsView> StructureDetailsView = PropertyEditorModule.CreateStructureDetailView(DetailsViewArgs, StructViewArgs, StructOnScope);

		Window->SetContent
		(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				StructureDetailsView->GetWidget().ToSharedRef()
			]
		    + SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SNew(SButton).Text(LOCTEXT("Cancel Button Text", "Cancel")).OnClicked(FOnClicked::CreateLambda([&bOutOperationCanceled, &Window] () { bOutOperationCanceled = true; Window->RequestDestroyWindow(); return FReply::Handled(); }))
				]
				+ SHorizontalBox::Slot()
				[
					SNew(SButton).Text(LOCTEXT("Import Button Text", "Import")).OnClicked(FOnClicked::CreateLambda([&Window] () { Window->RequestDestroyWindow(); return FReply::Handled(); }))
				]
				+ SHorizontalBox::Slot()
				[
					SNew(SButton).Text(LOCTEXT("Import All Button Text", "Import All")).OnClicked(FOnClicked::CreateLambda([this, &Window] () { bImportAll = true; Window->RequestDestroyWindow(); return FReply::Handled(); }))
				]
			]
		);

		FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);
	}

	UAnimSequence* NewAsset = nullptr;
	if (AutomatedImportSettings.Skeleton && !bOutOperationCanceled)
	{
		YAML::Node config = YAML::Load(std::string((char*)(Buffer), size_t(BufferEnd - Buffer)));
		YAML::Node AnimationClipNode = config["AnimationClip"];

		TMap<FName, FInterpCurveQuat> InterpCurveQuatByBoneMap;
		TMap<FName, FInterpCurveVector> InterpCurveTranslationByBoneMap;
		TMap<FName, FInterpCurveVector> InterpCurveScaleByBoneMap;
		float MinTimeDiff = 10000000.f;
		float MaxTime = 0.f;

		////Rotations m_RotationCurves
		{
			YAML::Node m_RotationCurvesNode = AnimationClipNode["m_RotationCurves"];
			bool bIsFirst = true;
			for (const YAML::Node& curveNode : m_RotationCurvesNode)
			{
				YAML::Node pathNode = curveNode["path"];
				const FName BoneName = GetBoneNameFromActorPath(pathNode.as<std::string>());
				FInterpCurveQuat& CurveQuat = InterpCurveQuatByBoneMap.FindOrAdd(BoneName);

				YAML::Node curvePointNodes = curveNode["curve"]["m_Curve"];
				float LastTime = -100000.f;
				for (const YAML::Node& pointNode : curvePointNodes)
				{
					const float Time = pointNode["time"].as<float>();
					FQuat Quat;
					Quat.X = -pointNode["value"]["x"].as<float>();
					Quat.Y = -pointNode["value"]["y"].as<float>();
					Quat.Z = pointNode["value"]["z"].as<float>();
					Quat.W = pointNode["value"]["w"].as<float>();
					Quat.Normalize();
					const int32 PointIndex = CurveQuat.AddPoint(Time, Quat);

					if (Time - LastTime > 0.f)
						MinTimeDiff = FMath::Min(Time - LastTime, MinTimeDiff);
					LastTime = Time;
					MaxTime = FMath::Max(Time, MaxTime);
				}
				CurveQuat.AutoSetTangents();
				bIsFirst = false;
			}
		}

		//Positions m_PositionCurves
		{
			YAML::Node m_CurvesNode = AnimationClipNode["m_PositionCurves"];
			for (const YAML::Node& curveNode : m_CurvesNode)
			{
				YAML::Node pathNode = curveNode["path"];
				const FName BoneName = GetBoneNameFromActorPath(pathNode.as<std::string>());
				FInterpCurveVector& TranslationCurve = InterpCurveTranslationByBoneMap.FindOrAdd(BoneName);

				YAML::Node curvePointNodes = curveNode["curve"]["m_Curve"];
				float LastTime = -100000.f;
				for (const YAML::Node& pointNode : curvePointNodes)
				{
					const float Time = pointNode["time"].as<float>();
					FVector Translation;
					Translation.X = -pointNode["value"]["x"].as<float>() * 100.f;
					Translation.Y = -pointNode["value"]["y"].as<float>() * 100.f;
					Translation.Z = pointNode["value"]["z"].as<float>() * 100.f;
					const int32 PointIndex = TranslationCurve.AddPoint(Time, Translation);

					if (Time - LastTime > 0.f)
						MinTimeDiff = FMath::Min(Time - LastTime, MinTimeDiff);
					LastTime = Time;
					MaxTime = FMath::Max(Time, MaxTime);
				}
				TranslationCurve.AutoSetTangents();
			}
		}

		//Scales m_ScaleCurves
		{
			YAML::Node m_CurvesNode = AnimationClipNode["m_ScaleCurves"];
			for (const YAML::Node& curveNode : m_CurvesNode)
			{
				YAML::Node pathNode = curveNode["path"];
				const FName BoneName = GetBoneNameFromActorPath(pathNode.as<std::string>());
				FInterpCurveVector& ScaleCurve = InterpCurveScaleByBoneMap.FindOrAdd(BoneName);

				YAML::Node curvePointNodes = curveNode["curve"]["m_Curve"];
				float LastTime = -100000.f;
				for (const YAML::Node& pointNode : curvePointNodes)
				{
					const float Time = pointNode["time"].as<float>();
					FVector Translation;
					Translation.X = pointNode["value"]["x"].as<float>();
					Translation.Y = pointNode["value"]["y"].as<float>();
					Translation.Z = pointNode["value"]["z"].as<float>();
					const int32 PointIndex = ScaleCurve.AddPoint(Time, Translation);

					if (Time - LastTime > 0.f)
						MinTimeDiff = FMath::Min(Time - LastTime, MinTimeDiff);
					LastTime = Time;
					MaxTime = FMath::Max(Time, MaxTime);
				}
				ScaleCurve.AutoSetTangents();
			}
		}

		ensure(MinTimeDiff > 0.f);
		if (MaxTime <= KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogSimpleUnityImport, Error, TEXT("Error importing, animation time couldn't be deducted !"));
			bOutOperationCanceled = true;
			return nullptr;
		}

		NewAsset = NewObject<UAnimSequence>(InParent, InClass, InName, Flags);
		NewAsset->SetSkeleton(AutomatedImportSettings.Skeleton);
		int32 NumFrames = 0;

		{
			for (const TPair<FName, FInterpCurveQuat>& CurveQuatRotPair : InterpCurveQuatByBoneMap)
			{
				const FInterpCurveQuat& CurveQuatRot = CurveQuatRotPair.Value;
				const int32 TrackIndex = NewAsset->AddNewRawTrack(CurveQuatRotPair.Key);
				FRawAnimSequenceTrack& RawAnimTrack = NewAsset->GetRawAnimationTrack(TrackIndex);

				FQuat LastQuat = FQuat::Identity;
				for (float Time = 0.f; Time < MaxTime + KINDA_SMALL_NUMBER; Time += MinTimeDiff)
				{
					FQuat Quat = CurveQuatRot.Eval(Time, LastQuat);
					Quat.Normalize();
					RawAnimTrack.RotKeys.Add(Quat);
					LastQuat = Quat;
				}
				NumFrames = FMath::Max(NumFrames, RawAnimTrack.RotKeys.Num());
			}
		}

		const TArray<FTransform> RefPoses = AutomatedImportSettings.Skeleton->GetRefLocalPoses();

		{
			for (const TPair<FName, FInterpCurveVector>& CurveTranslationPair : InterpCurveTranslationByBoneMap)
			{
				const FInterpCurveVector& CurveTranslation = CurveTranslationPair.Value;
				const int32 TrackIndex = NewAsset->AddNewRawTrack(CurveTranslationPair.Key);
				FRawAnimSequenceTrack& RawAnimTrack = NewAsset->GetRawAnimationTrack(TrackIndex);

				if (RawAnimTrack.RotKeys.Num() == 0)
					RawAnimTrack.RotKeys.Add(FQuat::Identity);

				FVector LastPos(0.f, 0.f, 0.f);
				for (float Time = 0.f; Time < MaxTime + KINDA_SMALL_NUMBER; Time += MinTimeDiff)
				{
					FVector Pos = CurveTranslation.Eval(Time, LastPos);
					RawAnimTrack.PosKeys.Add(Pos);
					LastPos = Pos;
				}
				NumFrames = FMath::Max(NumFrames, RawAnimTrack.PosKeys.Num());
			}
		}

		{
			for (const TPair<FName, FInterpCurveVector>& CurveScalePair : InterpCurveScaleByBoneMap)
			{
				const FInterpCurveVector& CurveScale = CurveScalePair.Value;
				const int32 TrackIndex = NewAsset->AddNewRawTrack(CurveScalePair.Key);
				FRawAnimSequenceTrack& RawAnimTrack = NewAsset->GetRawAnimationTrack(TrackIndex);

				if (RawAnimTrack.RotKeys.Num() == 0)
					RawAnimTrack.RotKeys.Add(FQuat::Identity);

				FVector LastPos(1.f, 1.f, 1.f);
				for (float Time = 0.f; Time < MaxTime + KINDA_SMALL_NUMBER; Time += MinTimeDiff)
				{
					FVector Pos = CurveScale.Eval(Time, LastPos);
					RawAnimTrack.ScaleKeys.Add(Pos);
					LastPos = Pos;
				}
				NumFrames = FMath::Max(NumFrames, RawAnimTrack.ScaleKeys.Num());
			}
		}

		NewAsset->SequenceLength = MaxTime * AutomatedImportSettings.ImportTimeRate;
		NewAsset->RateScale = 1.f;
		NewAsset->ImportFileFramerate = 1.f / MinTimeDiff;
		NewAsset->ImportResampleFramerate = NewAsset->ImportFileFramerate * AutomatedImportSettings.ImportTimeRate;
		NewAsset->AssetImportData->AddFileName(CurrentFilename, 0);
		NewAsset->SetRawNumberOfFrame(NumFrames);
		NewAsset->PostProcessSequence();
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, NewAsset);

	return NewAsset;
}

#undef LOCTEXT_NAMESPACE

