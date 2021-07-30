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

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SimpleUnityAnimImportFactory.generated.h"

USTRUCT(BlueprintType)
struct FSimpleUnityAnimImportSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = SimpleUnityAnimImportSettings)
	class USkeleton* Skeleton = nullptr;

	UPROPERTY(EditAnywhere, Category = SimpleUnityAnimImportSettings)
	float ImportTimeRate = 1.f;
};

UCLASS(hidecategories=Object)
class USimpleUnityAnimImportImportFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	//~ Begin UFactory Interface
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	virtual bool DoesSupportClass(UClass * Class) override;
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual void CleanUp() override;

public:
	UPROPERTY(BlueprintReadWrite, Category="Automation")
	FSimpleUnityAnimImportSettings AutomatedImportSettings;

	UPROPERTY()
	bool bImportAll = false;
};

