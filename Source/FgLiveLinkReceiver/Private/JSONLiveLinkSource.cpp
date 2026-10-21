// Copyright 2023 FACEGOOD, Inc. All Rights Reserved.

#include "JSONLiveLinkSource.h"

#include "JSONDatagramReassembler.h"
#include "JSONFrameRateEstimator.h"
#include "ILiveLinkClient.h"
#include "LiveLinkTypes.h"
#include "Async/Async.h"
#include "Common/UdpSocketBuilder.h"
#include "Containers/StringConv.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformTLS.h"
#include "Json.h"
#include "Roles/LiveLinkBasicRole.h"
#include "Roles/LiveLinkBasicTypes.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#define LOCTEXT_NAMESPACE "JSONLiveLinkSource"

#define RECV_BUFFER_SIZE 1024 * 1024
#define SOCKET_RECEIVE_BUFFER_SIZE 4 * 1024 * 1024

FJSONLiveLinkSource::FJSONLiveLinkSource(FIPv4Endpoint InEndpoint)
	: Client(nullptr)
	, Socket(nullptr)
	, SocketSubsystem(nullptr)
	, Stopping(false)
	, Thread(nullptr)
	, WaitTime(FTimespan::FromMilliseconds(100))
{
	// defaults
	DeviceEndpoint = InEndpoint;
	DatagramReassembler = MakeUnique<FJSONDatagramReassembler>();

	SourceStatus = LOCTEXT("SourceStatus_DeviceNotFound", "Device Not Found");
	SourceType = LOCTEXT("JSONLiveLinkSourceType", "FaceGoodAnimSource");
	SourceMachineName = LOCTEXT("JSONLiveLinkSourceMachineName", "localhost");

	//setup socket
	if (DeviceEndpoint.Address.IsMulticastAddress())
	{
		Socket = FUdpSocketBuilder(TEXT("JSONSOCKET"))
			.AsNonBlocking()
			.AsReusable()
			.BoundToPort(DeviceEndpoint.Port)
			.WithReceiveBufferSize(SOCKET_RECEIVE_BUFFER_SIZE)

			.BoundToAddress(FIPv4Address::Any)
			.JoinedToGroup(DeviceEndpoint.Address)
			.WithMulticastLoopback()
			.WithMulticastTtl(2);

	}
	else
	{
		Socket = FUdpSocketBuilder(TEXT("JSONSOCKET"))
			.AsNonBlocking()
			.AsReusable()
			.BoundToAddress(DeviceEndpoint.Address)
			.BoundToPort(DeviceEndpoint.Port)
			.WithReceiveBufferSize(SOCKET_RECEIVE_BUFFER_SIZE);
	}

	RecvBuffer.SetNumUninitialized(RECV_BUFFER_SIZE);

	if ((Socket != nullptr) && (Socket->GetSocketType() == SOCKTYPE_Datagram))
	{
		SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		SourceStatus = LOCTEXT("SourceStatus_WaitingForClient", "Waiting for Live Link Client");
	}
}

FJSONLiveLinkSource::~FJSONLiveLinkSource()
{
	Stop();
	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	if (Socket != nullptr)
	{
		Socket->Close();
		if (SocketSubsystem != nullptr)
		{
			SocketSubsystem->DestroySocket(Socket);
		}
	}
}

void FJSONLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SourceGuid = InSourceGuid;

	if (Client != nullptr && Socket != nullptr && Thread == nullptr)
	{
		Start();
		SourceStatus = LOCTEXT("SourceStatus_Receiving", "Receiving");
	}
}


bool FJSONLiveLinkSource::IsSourceStillValid() const
{
	// Source is valid if we have a valid thread and socket
	bool bIsSourceValid = !Stopping && Thread != nullptr && Socket != nullptr;
	return bIsSourceValid;
}


bool FJSONLiveLinkSource::RequestSourceShutdown()
{
	Stop();

	if (Thread != nullptr && Thread->GetThreadID() != FPlatformTLS::GetCurrentThreadId())
	{
		Thread->WaitForCompletion();
	}

	if (DatagramReassembler.IsValid())
	{
		DatagramReassembler->Reset();
	}
	LastSubjectFrameIds.Reset();
	SubjectFrameRateEstimators.Reset();

	return true;
}
// FRunnable interface

void FJSONLiveLinkSource::Start()
{
	if (Thread != nullptr || Socket == nullptr)
	{
		return;
	}

	ThreadName = "JSON UDP Receiver ";
	ThreadName.AppendInt(FAsyncThreadIndex::GetNext());

	Thread = FRunnableThread::Create(this, *ThreadName, 128 * 1024, TPri_AboveNormal, FPlatformAffinity::GetPoolThreadMask());
}

void FJSONLiveLinkSource::Stop()
{
	Stopping = true;
}

uint32 FJSONLiveLinkSource::Run()
{
	if (Socket == nullptr || SocketSubsystem == nullptr)
	{
		return 1;
	}

	TSharedRef<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();
	TArray<TArray<uint8>> CompleteMessages;

	while (!Stopping)
	{
		if (Socket->Wait(ESocketWaitConditions::WaitForRead, WaitTime))
		{
			uint32 Size;

			while (Socket->HasPendingData(Size))
			{
				int32 Read = 0;

				if (Socket->RecvFrom(RecvBuffer.GetData(), RecvBuffer.Num(), Read, *Sender))
				{
					if (Read > 0)
					{
						CompleteMessages.Reset();
						if (DatagramReassembler.IsValid())
						{
							DatagramReassembler->AddPacket(
								Sender->ToString(true),
								RecvBuffer.GetData(),
								Read,
								FPlatformTime::Seconds(),
								CompleteMessages);

							for (const TArray<uint8>& CompleteMessage : CompleteMessages)
							{
								HandleReceivedData(
									CompleteMessage.GetData(),
									CompleteMessage.Num());
							}
						}
						else
						{
							HandleReceivedData(RecvBuffer.GetData(), Read);
						}
					}
				}
			}
		}
	}
	return 0;
}

void FJSONLiveLinkSource::BuildMetaHumanPropertySet(
	const TArray<TPair<FName, float>>& InputProperties,
	TArray<FName>& OutPropertyNames,
	TArray<float>& OutPropertyValues)
{
	OutPropertyNames.Reset();
	OutPropertyValues.Reset();

	TSet<FName> SeenPropertyNames;
	auto AddProperty = [&OutPropertyNames, &OutPropertyValues, &SeenPropertyNames](
		const FName PropertyName,
		const float PropertyValue)
	{
		if (PropertyName.IsNone() || SeenPropertyNames.Contains(PropertyName))
		{
			return;
		}

		SeenPropertyNames.Add(PropertyName);
		OutPropertyNames.Add(PropertyName);
		OutPropertyValues.Add(PropertyValue);
	};

	for (const TPair<FName, float>& InputProperty : InputProperties)
	{
		AddProperty(InputProperty.Key, InputProperty.Value);

		const FString PropertyName = InputProperty.Key.ToString();
		FString BaseName;
		FString ComponentName;
		if (!PropertyName.Split(TEXT("."), &BaseName, &ComponentName))
		{
			const bool bCanUseDefaultYChannel =
				PropertyName.StartsWith(TEXT("CTRL_"), ESearchCase::CaseSensitive)
				&& !PropertyName.StartsWith(TEXT("CTRL_expressions_"), ESearchCase::CaseSensitive)
				&& !PropertyName.StartsWith(TEXT("CTRL_rigLogic_"), ESearchCase::CaseSensitive);
			if (bCanUseDefaultYChannel)
			{
				AddProperty(FName(*FString::Printf(TEXT("%s.ty"), *PropertyName)), InputProperty.Value);
			}
			continue;
		}

		FString MetaHumanComponentName;
		if (ComponentName.Equals(TEXT("translateX"), ESearchCase::IgnoreCase))
		{
			MetaHumanComponentName = TEXT("tx");
		}
		else if (ComponentName.Equals(TEXT("translateY"), ESearchCase::IgnoreCase))
		{
			MetaHumanComponentName = TEXT("ty");
		}
		else if (ComponentName.Equals(TEXT("translateZ"), ESearchCase::IgnoreCase))
		{
			MetaHumanComponentName = TEXT("tz");
		}

		if (!MetaHumanComponentName.IsEmpty())
		{
			AddProperty(
				FName(*FString::Printf(TEXT("%s.%s"), *BaseName, *MetaHumanComponentName)),
				InputProperty.Value);
		}
	}

	// MetaHuman's Basic LiveLink consumers expect these bookkeeping properties.
	// Explicit values from the sender take precedence over these defaults.
	AddProperty(FName(TEXT("HeadControlSwitch")), 0.0f);
	AddProperty(FName(TEXT("HeadRoll")), 0.0f);
	AddProperty(FName(TEXT("HeadPitch")), 0.0f);
	AddProperty(FName(TEXT("HeadYaw")), 0.0f);
	AddProperty(FName(TEXT("HeadTranslationX")), 0.0f);
	AddProperty(FName(TEXT("HeadTranslationY")), 0.0f);
	AddProperty(FName(TEXT("HeadTranslationZ")), 0.0f);
	AddProperty(FName(TEXT("MHFDSVersion")), 1.0f);
	AddProperty(FName(TEXT("DisableFaceOverride")), 1.0f);
}

bool FJSONLiveLinkSource::ShouldAcceptSubjectFrame(
	const int32 FrameId,
	int32& InOutLastAcceptedFrameId)
{
	if (FrameId == INDEX_NONE)
	{
		return true;
	}

	if (InOutLastAcceptedFrameId == INDEX_NONE || FrameId > InOutLastAcceptedFrameId)
	{
		InOutLastAcceptedFrameId = FrameId;
		return true;
	}

	// A large backwards jump is treated as a sender restart. Small backwards
	// jumps are stale datagrams and must not move the animation backwards.
	constexpr int32 MaxOutOfOrderBackwardsFrames = 600;
	if (InOutLastAcceptedFrameId - FrameId > MaxOutOfOrderBackwardsFrames)
	{
		InOutLastAcceptedFrameId = FrameId;
		return true;
	}

	return false;
}

void FJSONLiveLinkSource::HandleReceivedData(const uint8* Data, int32 NumBytes)
{
	if (Client == nullptr || Data == nullptr || NumBytes <= 0)
	{
		return;
	}

	const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Data), NumBytes);
	const FString JsonString(Converter.Length(), Converter.Get());

	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return;
	}

	for (const auto& JsonField : JsonObject->Values)
	{
		if (!JsonField.Value.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject> SubjectObject = JsonField.Value->AsObject();
		const FName SubjectName(*JsonField.Key);
		if (!SubjectObject.IsValid() || SubjectName.IsNone())
		{
			continue;
		}

		TArray<TPair<FName, float>> InputProperties;
		const TArray<TSharedPtr<FJsonValue>>* JsonProperties = nullptr;
		if (SubjectObject->TryGetArrayField(TEXT("Properties"), JsonProperties)
			&& JsonProperties != nullptr)
		{
			InputProperties.Reserve(JsonProperties->Num());
			for (const TSharedPtr<FJsonValue>& JsonProperty : *JsonProperties)
			{
				if (!JsonProperty.IsValid())
				{
					continue;
				}

				const TSharedPtr<FJsonObject> PropertyObject = JsonProperty->AsObject();
				if (!PropertyObject.IsValid())
				{
					continue;
				}

				FString PropertyName;
				float PropertyValue = 0.0f;
				if (PropertyObject->TryGetStringField(TEXT("Name"), PropertyName)
					&& PropertyObject->TryGetNumberField(TEXT("Value"), PropertyValue))
				{
					InputProperties.Emplace(FName(*PropertyName), PropertyValue);
				}
			}
		}

		TArray<FName> PropertyNames;
		TArray<float> PropertyValues;
		BuildMetaHumanPropertySet(InputProperties, PropertyNames, PropertyValues);

		int32 FrameId = INDEX_NONE;
		SubjectObject->TryGetNumberField(TEXT("FrameId"), FrameId);

		int32* LastAcceptedFrameId = LastSubjectFrameIds.Find(SubjectName);
		if (LastAcceptedFrameId == nullptr)
		{
			LastSubjectFrameIds.Add(SubjectName, INDEX_NONE);
			LastAcceptedFrameId = LastSubjectFrameIds.Find(SubjectName);
		}

		if (LastAcceptedFrameId == nullptr
			|| !ShouldAcceptSubjectFrame(FrameId, *LastAcceptedFrameId))
		{
			continue;
		}

		FFrameRate SceneFrameRate(FJSONFrameRateEstimator::NominalSceneFramesPerSecond, 1);
		double EstimatedSourceFramesPerSecond = 0.0;
		if (FrameId != INDEX_NONE)
		{
			TUniquePtr<FJSONFrameRateEstimator>& FrameRateEstimator =
				SubjectFrameRateEstimators.FindOrAdd(SubjectName);
			if (!FrameRateEstimator.IsValid())
			{
				FrameRateEstimator = MakeUnique<FJSONFrameRateEstimator>();
			}

			FrameRateEstimator->AddFrame(FrameId, FPlatformTime::Seconds());
			EstimatedSourceFramesPerSecond = FrameRateEstimator->GetFramesPerSecond();
			SceneFrameRate = FrameRateEstimator->GetSceneFrameRate();
		}

		const TArray<FName>* ExistingPropertyNames = SubjectPropertyNames.Find(SubjectName);
		if (ExistingPropertyNames == nullptr || *ExistingPropertyNames != PropertyNames)
		{
			FLiveLinkStaticDataStruct StaticDataStruct(FLiveLinkBaseStaticData::StaticStruct());
			if (!StaticDataStruct.IsValid())
			{
				continue;
			}

			FLiveLinkBaseStaticData& StaticData =
				*StaticDataStruct.Cast<FLiveLinkBaseStaticData>();
			StaticData.PropertyNames = PropertyNames;

			Client->PushSubjectStaticData_AnyThread(
				{ SourceGuid, SubjectName },
				ULiveLinkBasicRole::StaticClass(),
				MoveTemp(StaticDataStruct));
			SubjectPropertyNames.Add(SubjectName, PropertyNames);
		}

		FLiveLinkFrameDataStruct FrameDataStruct(FLiveLinkBaseFrameData::StaticStruct());
		if (!FrameDataStruct.IsValid())
		{
			continue;
		}

		FLiveLinkBaseFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkBaseFrameData>();
		FrameData.PropertyValues = PropertyValues;
		FrameData.FrameId = FrameId;
		FrameData.MetaData.SceneTime.Time.FrameNumber = FrameData.FrameId;
		FrameData.MetaData.SceneTime.Rate = SceneFrameRate;
		if (EstimatedSourceFramesPerSecond > 0.0)
		{
			FrameData.MetaData.StringMetaData.Add(
				FName(TEXT("EstimatedSourceFPS")),
				FString::Printf(TEXT("%.3f"), EstimatedSourceFramesPerSecond));
		}
		FrameData.MetaData.StringMetaData.Add(
			FName(TEXT("SceneTimeFrameRate")),
			FString::Printf(
				TEXT("%d/%d"),
				SceneFrameRate.Numerator,
				SceneFrameRate.Denominator));

		Client->PushSubjectFrameData_AnyThread(
			{ SourceGuid, SubjectName },
			MoveTemp(FrameDataStruct));
	}
}

#undef LOCTEXT_NAMESPACE
