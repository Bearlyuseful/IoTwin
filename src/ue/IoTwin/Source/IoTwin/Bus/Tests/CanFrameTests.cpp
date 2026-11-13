// IoTwin - automation tests for the CAN text codec.
// Run: UnrealEditor-Cmd IoTwin.uproject -ExecCmds="Automation RunTests IoTwin;Quit" -unattended -nullrhi

#include "Bus/CanFrame.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIoTwinCanFrameRoundTripTest, "IoTwin.Bus.CanFrame.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIoTwinCanFrameRoundTripTest::RunTest(const FString& Parameters)
{
	const TCHAR* Samples[] = {
		TEXT("123#DEADBEEF"), TEXT("7FF#0011223344556677"), TEXT("000#"),
		TEXT("1FFFFFFF#AA"), TEXT("123#R"), TEXT("123#R8")};
	for (const TCHAR* Sample : Samples)
	{
		FCanFrame Frame;
		TestTrue(FString::Printf(TEXT("parse %s"), Sample), FCanFrame::Parse(Sample, Frame));
		TestEqual(FString::Printf(TEXT("encode %s"), Sample), Frame.ToString(), FString(Sample));
	}

	FCanFrame Lower;
	TestTrue(TEXT("lower case with separators"), FCanFrame::Parse(TEXT("1ab#de.ad"), Lower));
	TestEqual(TEXT("normalised"), Lower.ToString(), FString(TEXT("1AB#DEAD")));
	TestTrue(TEXT("bit 0 of 0xDE is 0"), !Lower.GetBit(0, 0));
	TestTrue(TEXT("bit 1 of 0xDE is 1"), Lower.GetBit(0, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIoTwinCanFrameRejectTest, "IoTwin.Bus.CanFrame.RejectsMalformed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIoTwinCanFrameRejectTest::RunTest(const FString& Parameters)
{
	const TCHAR* Invalid[] = {
		TEXT(""), TEXT("123"), TEXT("12#00"), TEXT("800#00"), TEXT("20000000#00"), TEXT("123#0"),
		TEXT("123#XYZW"), TEXT("123#001122334455667788"), TEXT("123##11"), TEXT("123#R9"), TEXT("123#.11")};
	for (const TCHAR* Text : Invalid)
	{
		FCanFrame Frame;
		TestFalse(FString::Printf(TEXT("reject '%s'"), Text), FCanFrame::Parse(Text, Frame));
	}

	int32 Id = 0;
	TestTrue(TEXT("parse id 0x100"), FCanFrame::ParseId(TEXT("0x100"), false, Id) && Id == 0x100);
	TestFalse(TEXT("reject standard id 800"), FCanFrame::ParseId(TEXT("800"), false, Id));
	TestTrue(TEXT("accept extended id 800"), FCanFrame::ParseId(TEXT("800"), true, Id));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
