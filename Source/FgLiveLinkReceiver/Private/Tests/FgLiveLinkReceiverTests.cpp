// Copyright 2023 FACEGOOD, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/StringConv.h"
#include "JSONDatagramReassembler.h"
#include "JSONFrameRateEstimator.h"
#include "JSONLiveLinkSource.h"

namespace
{
	TArray<uint8> ToUtf8Bytes(const TCHAR* Text)
	{
		const FTCHARToUTF8 Converter(Text);
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Converter.Length());
		FMemory::Memcpy(Bytes.GetData(), Converter.Get(), Converter.Length());
		return Bytes;
	}

	FString FromUtf8Bytes(const TArray<uint8>& Bytes)
	{
		const FUTF8ToTCHAR Converter(
			reinterpret_cast<const ANSICHAR*>(Bytes.GetData()),
			Bytes.Num());
		return FString(Converter.Length(), Converter.Get());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFgLiveLinkDatagramReassemblyTest,
	"FgLiveLinkReceiver.Datagram.ReassemblesSplitJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFgLiveLinkDatagramReassemblyTest::RunTest(const FString& Parameters)
{
	FJSONDatagramReassembler Reassembler;
	TArray<TArray<uint8>> CompleteMessages;

	const TArray<uint8> FirstPart = ToUtf8Bytes(TEXT("{\"MH_Alice\":{\"FrameId\":1,\"Properties\":["));
	const TArray<uint8> SecondPart = ToUtf8Bytes(TEXT("{\"Name\":\"CTRL_L_eye_blink\",\"Value\":0.5}]}}"));

	Reassembler.AddPacket(
		TEXT("127.0.0.1:54321"),
		FirstPart.GetData(),
		FirstPart.Num(),
		0.0,
		CompleteMessages);
	TestEqual(TEXT("Incomplete datagram is retained"), CompleteMessages.Num(), 0);

	Reassembler.AddPacket(
		TEXT("127.0.0.1:54321"),
		SecondPart.GetData(),
		SecondPart.Num(),
		0.01,
		CompleteMessages);

	TestEqual(TEXT("Split JSON produces one complete message"), CompleteMessages.Num(), 1);
	if (CompleteMessages.Num() == 1)
	{
		TestEqual(
			TEXT("Reassembled JSON bytes match"),
			FromUtf8Bytes(CompleteMessages[0]),
			FString(TEXT("{\"MH_Alice\":{\"FrameId\":1,\"Properties\":[{\"Name\":\"CTRL_L_eye_blink\",\"Value\":0.5}]}}")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFgLiveLinkDatagramMultipleJsonTest,
	"FgLiveLinkReceiver.Datagram.ExtractsMultipleJsonObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFgLiveLinkDatagramMultipleJsonTest::RunTest(const FString& Parameters)
{
	FJSONDatagramReassembler Reassembler;
	TArray<TArray<uint8>> CompleteMessages;

	const TArray<uint8> Payload = ToUtf8Bytes(
		TEXT("{\"MH_Alice\":{\"FrameId\":1}} {\"MH_Bob\":{\"FrameId\":1}}"));
	Reassembler.AddPacket(
		TEXT("127.0.0.1:54321"),
		Payload.GetData(),
		Payload.Num(),
		0.0,
		CompleteMessages);

	TestEqual(TEXT("Both concatenated JSON objects are extracted"), CompleteMessages.Num(), 2);
	if (CompleteMessages.Num() == 2)
	{
		TestEqual(
			TEXT("First subject remains first"),
			FromUtf8Bytes(CompleteMessages[0]),
			FString(TEXT("{\"MH_Alice\":{\"FrameId\":1}}")));
		TestEqual(
			TEXT("Second subject is extracted independently"),
			FromUtf8Bytes(CompleteMessages[1]),
			FString(TEXT("{\"MH_Bob\":{\"FrameId\":1}}")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFgLiveLinkDatagramJsonStringBracesTest,
	"FgLiveLinkReceiver.Datagram.IgnoresBracesInsideStrings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFgLiveLinkDatagramJsonStringBracesTest::RunTest(const FString& Parameters)
{
	FJSONDatagramReassembler Reassembler;
	TArray<TArray<uint8>> CompleteMessages;

	const TArray<uint8> Payload = ToUtf8Bytes(
		TEXT("{\"MH_Alice\":{\"Properties\":[{\"Name\":\"CTRL_test\",\"Value\":1}],\"Tag\":\"{escaped}\"}}"));
	Reassembler.AddPacket(
		TEXT("127.0.0.1:54321"),
		Payload.GetData(),
		Payload.Num(),
		0.0,
		CompleteMessages);

	TestEqual(TEXT("Braces inside JSON strings do not end the frame"), CompleteMessages.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFgLiveLinkDatagramTimeoutTest,
	"FgLiveLinkReceiver.Datagram.DiscardsStalePartialJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFgLiveLinkDatagramTimeoutTest::RunTest(const FString& Parameters)
{
	FJSONDatagramReassembler Reassembler;
	TArray<TArray<uint8>> CompleteMessages;

	const TArray<uint8> FirstPart = ToUtf8Bytes(TEXT("{\"MH_Alice\":{\"FrameId\":1"));
	const TArray<uint8> StalePart = ToUtf8Bytes(TEXT("}}"));
	Reassembler.AddPacket(
		TEXT("127.0.0.1:54321"),
		FirstPart.GetData(),
		FirstPart.Num(),
		0.0,
		CompleteMessages);
	Reassembler.AddPacket(
		TEXT("127.0.0.1:54321"),
		StalePart.GetData(),
		StalePart.Num(),
		3.0,
		CompleteMessages);

	TestEqual(TEXT("Timed-out partial JSON is not completed by a stale suffix"), CompleteMessages.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFgLiveLinkMetaHumanPropertyAliasesTest,
	"FgLiveLinkReceiver.MetaHuman.BuildsBasicPropertyAliases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFgLiveLinkMetaHumanPropertyAliasesTest::RunTest(const FString& Parameters)
{
	TArray<TPair<FName, float>> InputProperties;
	InputProperties.Emplace(FName(TEXT("CTRL_C_jaw.translateX")), 0.25f);
	InputProperties.Emplace(FName(TEXT("CTRL_C_jaw.translateY")), 0.5f);
	InputProperties.Emplace(FName(TEXT("CTRL_L_eye_blink")), 0.4f);

	TArray<FName> PropertyNames;
	TArray<float> PropertyValues;
	FJSONLiveLinkSource::BuildMetaHumanPropertySet(
		InputProperties,
		PropertyNames,
		PropertyValues);

	TestEqual(
		TEXT("Property names and values stay aligned"),
		PropertyNames.Num(),
		PropertyValues.Num());

	TestTrue(
		TEXT("Original translateX property is preserved"),
		PropertyNames.Contains(FName(TEXT("CTRL_C_jaw.translateX"))));
	TestTrue(
		TEXT("Original translateY property is preserved"),
		PropertyNames.Contains(FName(TEXT("CTRL_C_jaw.translateY"))));
	TestTrue(
		TEXT("MetaHuman tx property is added"),
		PropertyNames.Contains(FName(TEXT("CTRL_C_jaw.tx"))));
	TestTrue(
		TEXT("MetaHuman ty property is added"),
		PropertyNames.Contains(FName(TEXT("CTRL_C_jaw.ty"))));
	TestTrue(
		TEXT("Scalar ControlRig value receives a MetaHuman ty alias"),
		PropertyNames.Contains(FName(TEXT("CTRL_L_eye_blink.ty"))));

	const int32 TxIndex = PropertyNames.IndexOfByKey(FName(TEXT("CTRL_C_jaw.tx")));
	const int32 TyIndex = PropertyNames.IndexOfByKey(FName(TEXT("CTRL_C_jaw.ty")));
	const int32 EyeBlinkTyIndex =
		PropertyNames.IndexOfByKey(FName(TEXT("CTRL_L_eye_blink.ty")));
	if (TxIndex != INDEX_NONE && TyIndex != INDEX_NONE)
	{
		TestEqual(TEXT("tx value matches translateX"), PropertyValues[TxIndex], 0.25f);
		TestEqual(TEXT("ty value matches translateY"), PropertyValues[TyIndex], 0.5f);
	}
	if (EyeBlinkTyIndex != INDEX_NONE)
	{
		TestEqual(
			TEXT("Scalar ty value matches the source value"),
			PropertyValues[EyeBlinkTyIndex],
			0.4f);
	}

	TestTrue(
		TEXT("Head pose properties are synthesized"),
		PropertyNames.Contains(FName(TEXT("HeadRoll")))
			&& PropertyNames.Contains(FName(TEXT("HeadPitch")))
			&& PropertyNames.Contains(FName(TEXT("HeadYaw")))
			&& PropertyNames.Contains(FName(TEXT("HeadTranslationX")))
			&& PropertyNames.Contains(FName(TEXT("HeadTranslationY")))
			&& PropertyNames.Contains(FName(TEXT("HeadTranslationZ"))));

	const int32 VersionIndex = PropertyNames.IndexOfByKey(FName(TEXT("MHFDSVersion")));
	const int32 OverrideIndex = PropertyNames.IndexOfByKey(FName(TEXT("DisableFaceOverride")));
	const int32 HeadControlIndex = PropertyNames.IndexOfByKey(FName(TEXT("HeadControlSwitch")));
	if (HeadControlIndex != INDEX_NONE)
	{
		TestEqual(
			TEXT("HeadControlSwitch defaults off when no head pose is supplied"),
			PropertyValues[HeadControlIndex],
			0.0f);
	}

	if (VersionIndex != INDEX_NONE && OverrideIndex != INDEX_NONE)
	{
		TestEqual(TEXT("MHFDSVersion defaults to one"), PropertyValues[VersionIndex], 1.0f);
		TestEqual(
			TEXT("DisableFaceOverride defaults to one"),
			PropertyValues[OverrideIndex],
			1.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFgLiveLinkMetaHumanExplicitPropertiesTest,
	"FgLiveLinkReceiver.MetaHuman.PreservesExplicitProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFgLiveLinkMetaHumanExplicitPropertiesTest::RunTest(const FString& Parameters)
{
	TArray<TPair<FName, float>> InputProperties;
	InputProperties.Emplace(FName(TEXT("HeadRoll")), 12.0f);
	InputProperties.Emplace(FName(TEXT("MHFDSVersion")), 2.0f);

	TArray<FName> PropertyNames;
	TArray<float> PropertyValues;
	FJSONLiveLinkSource::BuildMetaHumanPropertySet(
		InputProperties,
		PropertyNames,
		PropertyValues);

	TestEqual(
		TEXT("HeadRoll appears once"),
		PropertyNames.FilterByPredicate(
			[](const FName& Name)
			{
				return Name == FName(TEXT("HeadRoll"));
			}).Num(),
		1);

	const int32 RollIndex = PropertyNames.IndexOfByKey(FName(TEXT("HeadRoll")));
	const int32 VersionIndex = PropertyNames.IndexOfByKey(FName(TEXT("MHFDSVersion")));
	if (RollIndex != INDEX_NONE && VersionIndex != INDEX_NONE)
	{
		TestEqual(TEXT("Explicit HeadRoll value is preserved"), PropertyValues[RollIndex], 12.0f);
		TestEqual(TEXT("Explicit MHFDSVersion value is preserved"), PropertyValues[VersionIndex], 2.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFgLiveLinkFrameOrderTest,
	"FgLiveLinkReceiver.Frames.RejectsStaleFrameIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFgLiveLinkFrameOrderTest::RunTest(const FString& Parameters)
{
	int32 LastAcceptedFrameId = INDEX_NONE;

	TestTrue(
		TEXT("First valid frame is accepted"),
		FJSONLiveLinkSource::ShouldAcceptSubjectFrame(10, LastAcceptedFrameId));
	TestTrue(
		TEXT("Newer frame is accepted"),
		FJSONLiveLinkSource::ShouldAcceptSubjectFrame(11, LastAcceptedFrameId));
	TestFalse(
		TEXT("Duplicate frame is rejected"),
		FJSONLiveLinkSource::ShouldAcceptSubjectFrame(11, LastAcceptedFrameId));
	TestFalse(
		TEXT("Out-of-order older frame does not move animation backwards"),
		FJSONLiveLinkSource::ShouldAcceptSubjectFrame(9, LastAcceptedFrameId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFgLiveLinkFrameRateEstimatorTest,
	"FgLiveLinkReceiver.Frames.EstimatesArrivalFrameRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFgLiveLinkFrameRateEstimatorTest::RunTest(const FString& Parameters)
{
	FJSONFrameRateEstimator Estimator;
	for (int32 FrameIndex = 0; FrameIndex <= 30; ++FrameIndex)
	{
		Estimator.AddFrame(FrameIndex, static_cast<double>(FrameIndex) / 30.0);
	}

	TestTrue(TEXT("Estimator produces an estimated frame rate"), Estimator.HasEstimate());
	TestTrue(
		TEXT("Estimated frame rate is near 30 fps"),
		FMath::IsNearlyEqual(Estimator.GetFramesPerSecond(), 30.0, 0.5));
	TestEqual(
		TEXT("Scene time uses the stable nominal frame-rate numerator"),
		Estimator.GetSceneFrameRate().Numerator,
		30);
	TestEqual(
		TEXT("Scene time uses the stable nominal frame-rate denominator"),
		Estimator.GetSceneFrameRate().Denominator,
		1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFgLiveLinkFrameRateEstimatorResetTest,
	"FgLiveLinkReceiver.Frames.ResetsFrameRateAfterFrameIdRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFgLiveLinkFrameRateEstimatorResetTest::RunTest(const FString& Parameters)
{
	FJSONFrameRateEstimator Estimator;
	for (int32 FrameIndex = 10000; FrameIndex <= 10030; ++FrameIndex)
	{
		Estimator.AddFrame(FrameIndex, static_cast<double>(FrameIndex - 10000) / 30.0);
	}

	TestTrue(TEXT("Initial estimate exists"), Estimator.HasEstimate());
	Estimator.AddFrame(0, 2.0);
	TestFalse(TEXT("Large FrameId backwards jump clears the estimate"), Estimator.HasEstimate());

	for (int32 FrameIndex = 1; FrameIndex <= 30; ++FrameIndex)
	{
		Estimator.AddFrame(FrameIndex, 2.0 + static_cast<double>(FrameIndex) / 60.0);
	}

	TestTrue(TEXT("Estimator recovers after restart"), Estimator.HasEstimate());
	TestTrue(
		TEXT("Recovered estimate is near 60 fps"),
		FMath::IsNearlyEqual(Estimator.GetFramesPerSecond(), 60.0, 0.5));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
