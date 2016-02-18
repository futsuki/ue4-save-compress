// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SaveCompressPluginPrivatePCH.h"
#include "SaveCompressPluginBPLibrary.h"
#include "PlatformFeatures.h"

#define LOG(txt) { GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, txt); };


USaveCompressPluginBPLibrary::USaveCompressPluginBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}

//float USaveCompressPluginBPLibrary::SaveCompressPluginSampleFunction(float Param)
//{
//	return -1;
//}

static const int SCP_UE4_SAVEGAME_FILE_TYPE_TAG = 0x53415643;		// "SAVC", not SAVG
static const int SCP_UE4_SAVEGAME_FILE_VERSION = 1;

bool USaveCompressPluginBPLibrary::SaveGameToSlotCompressed(USaveGame* SaveGameObject, const FString& SlotName, const int32 UserIndex) {

	bool bSuccess = false;

	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	// If we have a system and an object to save and a save name...
	if (SaveSystem && (SaveGameObject != NULL) && (SlotName.Len() > 0))
	{
		TArray<uint8> ObjectBytes;
		FMemoryWriter MemoryWriter(ObjectBytes, true);

		// write file type tag. identifies this file type and indicates it's using proper versioning
		// since older UE4 versions did not version this data.
		int32 FileTypeTag = SCP_UE4_SAVEGAME_FILE_TYPE_TAG;
		MemoryWriter << FileTypeTag;

		// Write version for this file format
		int32 SavegameFileVersion = SCP_UE4_SAVEGAME_FILE_VERSION;
		MemoryWriter << SavegameFileVersion;

		// Write out engine and UE4 version information
		MemoryWriter << GPackageFileUE4Version;
		FEngineVersion SavedEngineVersion = GEngineVersion;
		MemoryWriter << SavedEngineVersion;

		// Write the class name so we know what class to load to
		FString SaveGameClassName = SaveGameObject->GetClass()->GetName();
		MemoryWriter << SaveGameClassName;

		// Then save the object state, replacing object refs and names with strings
		//FObjectAndNameAsStringProxyArchive Ar(MemoryWriter, false);
		//SaveGameObject->Serialize(Ar);

		// Save Compressed Data
		TArray<uint8> CompressedData;
		FArchiveSaveCompressedProxy Compressor =
			FArchiveSaveCompressedProxy(CompressedData, ECompressionFlags::COMPRESS_ZLIB);
		SaveGameObject->Serialize(Compressor);
		Compressor.Flush();

		// Output Compressed Data
		MemoryWriter << CompressedData;

		// Stuff that data into the save system with the desired file name
		bSuccess = SaveSystem->SaveGame(false, *SlotName, UserIndex, ObjectBytes);

		Compressor.FlushCache();
		CompressedData.Empty();
		MemoryWriter.FlushCache();
		MemoryWriter.Close();
	}

	return bSuccess;
}

USaveGame* USaveCompressPluginBPLibrary::LoadGameFromSlotCompressed(const FString& SlotName, const int32 UserIndex)
{
	USaveGame* OutSaveGameObject = NULL;

	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	// If we have a save system and a valid name..
	if (SaveSystem && (SlotName.Len() > 0))
	{
		// Load raw data from slot
		TArray<uint8> ObjectBytes;
		bool bSuccess = SaveSystem->LoadGame(false, *SlotName, UserIndex, ObjectBytes);
		if (bSuccess)
		{
			FMemoryReader MemoryReader(ObjectBytes, true);

			int32 FileTypeTag;
			MemoryReader << FileTypeTag;

			int32 SavegameFileVersion;
			if (FileTypeTag != SCP_UE4_SAVEGAME_FILE_TYPE_TAG)
			{
				// this is an old saved game, back up the file pointer to the beginning and assume version 1
				MemoryReader.Seek(0);
				SavegameFileVersion = 1;

				// Note for 4.8 and beyond: if you get a crash loading a pre-4.8 version of your savegame file and 
				// you don't want to delete it, try uncommenting these lines and changing them to use the version 
				// information from your previous build. Then load and resave your savegame file.
				//MemoryReader.SetUE4Ver(MyPreviousUE4Version);				// @see GPackageFileUE4Version
				//MemoryReader.SetEngineVer(MyPreviousEngineVersion);		// @see GEngineVersion
			}
			else
			{
				// Read version for this file format
				MemoryReader << SavegameFileVersion;

				// Read engine and UE4 version information
				int32 SavedUE4Version;
				MemoryReader << SavedUE4Version;

				FEngineVersion SavedEngineVersion;
				MemoryReader << SavedEngineVersion;

				MemoryReader.SetUE4Ver(SavedUE4Version);
				MemoryReader.SetEngineVer(SavedEngineVersion);
			}

			// Get the class name
			FString SaveGameClassName;
			MemoryReader << SaveGameClassName;

			// Try and find it, and failing that, load it
			UClass* SaveGameClass = FindObject<UClass>(ANY_PACKAGE, *SaveGameClassName);
			if (SaveGameClass == NULL)
			{
				SaveGameClass = LoadObject<UClass>(NULL, *SaveGameClassName);
			}

			// If we have a class, try and load it.
			if (SaveGameClass != NULL)
			{
				OutSaveGameObject = NewObject<USaveGame>(GetTransientPackage(), SaveGameClass);

				//FObjectAndNameAsStringProxyArchive Ar(MemoryReader, true);
				//OutSaveGameObject->Serialize(Ar);

				// slice tail
				TArray<uint8> CompressedData;
				MemoryReader << CompressedData;

				FArchiveLoadCompressedProxy Decompressor =
					FArchiveLoadCompressedProxy(CompressedData, ECompressionFlags::COMPRESS_ZLIB);
				if (Decompressor.GetError())
				{
					LOG(TEXT("FArchiveLoadCompressedProxy>> ERROR : File Was Not Compressed "));
					return nullptr;
				}
				OutSaveGameObject->Serialize(Decompressor);
				Decompressor.Flush();

				Decompressor.FlushCache();
				CompressedData.Empty();
				MemoryReader.FlushCache();
				MemoryReader.Close();
			}
		}
	}

	return OutSaveGameObject;
}


