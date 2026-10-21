// Copyright 2023 FACEGOOD, Inc. All Rights Reserved.

#include "JSONDatagramReassembler.h"

namespace
{
	bool IsJsonWhitespace(const uint8 Byte)
	{
		return Byte == ' '
			|| Byte == '\t'
			|| Byte == '\r'
			|| Byte == '\n';
	}
}

void FJSONDatagramReassembler::AddPacket(
	const FString& PeerAddress,
	const uint8* Data,
	int32 NumBytes,
	double CurrentTimeSeconds,
	TArray<TArray<uint8>>& OutCompleteMessages)
{
	if (Data == nullptr || NumBytes <= 0)
	{
		return;
	}

	for (auto PeerIt = PeerStates.CreateIterator(); PeerIt; ++PeerIt)
	{
		if (CurrentTimeSeconds - PeerIt.Value().LastPacketTimeSeconds
			> PartialMessageTimeoutSeconds * 2.0)
		{
			PeerIt.RemoveCurrent();
		}
	}

	FPeerState& PeerState = PeerStates.FindOrAdd(PeerAddress);
	const bool bTimedOut =
		PeerState.PendingBytes.Num() > 0
		&& CurrentTimeSeconds - PeerState.LastPacketTimeSeconds > PartialMessageTimeoutSeconds;
	const bool bWouldExceedLimit = PeerState.PendingBytes.Num() + NumBytes > MaxPendingBytes;
	if (bTimedOut || bWouldExceedLimit)
	{
		PeerState.PendingBytes.Reset();
	}

	PeerState.LastPacketTimeSeconds = CurrentTimeSeconds;
	PeerState.PendingBytes.Append(Data, NumBytes);

	int32 Cursor = 0;
	while (Cursor < PeerState.PendingBytes.Num())
	{
		while (Cursor < PeerState.PendingBytes.Num()
			&& IsJsonWhitespace(PeerState.PendingBytes[Cursor]))
		{
			++Cursor;
		}

		if (Cursor >= PeerState.PendingBytes.Num())
		{
			break;
		}

		// The FaceGood packet is a top-level JSON object. Ignore bytes until the
		// next object opener so malformed prefixes do not permanently poison the
		// pending buffer.
		if (PeerState.PendingBytes[Cursor] != '{')
		{
			++Cursor;
			continue;
		}

		const int32 ObjectStart = Cursor;
		int32 ObjectDepth = 0;
		bool bInString = false;
		bool bEscaped = false;
		bool bComplete = false;

		for (; Cursor < PeerState.PendingBytes.Num(); ++Cursor)
		{
			const uint8 Byte = PeerState.PendingBytes[Cursor];

			if (bInString)
			{
				if (bEscaped)
				{
					bEscaped = false;
				}
				else if (Byte == '\\')
				{
					bEscaped = true;
				}
				else if (Byte == '"')
				{
					bInString = false;
				}
				continue;
			}

			if (Byte == '"')
			{
				bInString = true;
			}
			else if (Byte == '{')
			{
				++ObjectDepth;
			}
			else if (Byte == '}')
			{
				--ObjectDepth;
				if (ObjectDepth == 0)
				{
					TArray<uint8> CompleteMessage;
					CompleteMessage.Append(
						PeerState.PendingBytes.GetData() + ObjectStart,
						Cursor - ObjectStart + 1);
					OutCompleteMessages.Add(MoveTemp(CompleteMessage));

					++Cursor;
					bComplete = true;
					break;
				}
			}
		}

		if (!bComplete)
		{
			Cursor = ObjectStart;
			break;
		}
	}

	if (Cursor > 0)
	{
		PeerState.PendingBytes.RemoveAt(0, Cursor);
	}
}

void FJSONDatagramReassembler::Reset()
{
	PeerStates.Reset();
}
