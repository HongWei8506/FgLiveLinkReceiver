// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
// Copyright 2023 FACEGOOD, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkSource.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

class FRunnableThread;
class FSocket;
class FJSONDatagramReassembler;
class ILiveLinkClient;
class ISocketSubsystem;

class FGLIVELINKRECEIVER_API FJSONLiveLinkSource : public ILiveLinkSource, public FRunnable
{
public:

	FJSONLiveLinkSource(FIPv4Endpoint Endpoint);

	virtual ~FJSONLiveLinkSource();

	// Begin ILiveLinkSource Interface
	
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;

	virtual bool IsSourceStillValid() const override;

	virtual bool RequestSourceShutdown() override;

	virtual FText GetSourceType() const override { return SourceType; };
	virtual FText GetSourceMachineName() const override { return SourceMachineName; }
	virtual FText GetSourceStatus() const override { return SourceStatus; }

	// End ILiveLinkSource Interface

	// Begin FRunnable Interface

	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	void Start();
	virtual void Stop() override;
	virtual void Exit() override { }

	// End FRunnable Interface

	/**
	 * Builds the ordered property set consumed by MetaHuman's Basic LiveLink role.
	 * Existing names are preserved and explicit translateX/Y/Z names also receive
	 * tx/ty/tz aliases. Required MetaHuman bookkeeping properties are appended only
	 * when the source did not already provide them.
	 */
	static void BuildMetaHumanPropertySet(
		const TArray<TPair<FName, float>>& InputProperties,
		TArray<FName>& OutPropertyNames,
		TArray<float>& OutPropertyValues);

	/**
	 * Accepts new and forward-skipped frame IDs while rejecting duplicates and
	 * stale frames that arrive after a newer frame.
	 */
	static bool ShouldAcceptSubjectFrame(
		int32 FrameId,
		int32& InOutLastAcceptedFrameId);

	void HandleReceivedData(const uint8* Data, int32 NumBytes);

private:

	ILiveLinkClient* Client;

	// Our identifier in LiveLink
	FGuid SourceGuid;

	FText SourceType;
	FText SourceMachineName;
	FText SourceStatus;

	FIPv4Endpoint DeviceEndpoint;

	// Socket to receive data on
	FSocket* Socket;

	// Subsystem associated to Socket
	ISocketSubsystem* SocketSubsystem;

	// Threadsafe Bool for terminating the main thread loop
	FThreadSafeBool Stopping;

	// Thread to run socket operations on
	FRunnableThread* Thread;

	// Name of the sockets thread
	FString ThreadName;

	// Time to wait between attempted receives
	FTimespan WaitTime;

	// Property layout currently published for each subject
	TMap<FName, TArray<FName>> SubjectPropertyNames;

	// Last accepted frame for each subject, used to suppress duplicate/stale frames
	TMap<FName, int32> LastSubjectFrameIds;

	// Reassembles application-level split or concatenated JSON payloads
	TUniquePtr<FJSONDatagramReassembler> DatagramReassembler;

	// Buffer to receive socket data into
	TArray<uint8> RecvBuffer;
};
