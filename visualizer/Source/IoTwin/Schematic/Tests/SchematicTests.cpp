// IoTwin - automation tests for the schematic view model (DBC, system description, components).
// Run: UnrealEditor-Cmd IoTwin.uproject -ExecCmds="Automation RunTests IoTwin;Quit" -unattended -nullrhi

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Schematic/CanDatabase.h"
#include "Schematic/ComponentCatalog.h"
#include "Schematic/IoTwinSchematicSubsystem.h"
#include "Schematic/SystemDescription.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// 2147484160 = 0x80000000 | 0x200 (extended identifier 0x200).
	const TCHAR* TestDbc =
		TEXT("VERSION \"test\"\n")
		TEXT("BU_: LightECU Gateway\n")
		TEXT("BO_ 256 LAMP_CMD: 1 LightECU\n")
		TEXT(" SG_ LampOn : 0|1@1+ (1,0) [0|1] \"\" Gateway\n")
		TEXT("BO_ 2147484160 SENSOR: 2 Gateway\n")
		TEXT(" SG_ Temp : 7|12@0- (0.5,-40) [-100|100] \"degC\" LightECU\n")
		TEXT("CM_ BO_ 256 \"comment over two lines,\n")
		TEXT("BO_ 999 FAKE: 1 LightECU\";\n")
		TEXT("VAL_ 256 LampOn 0 \"Off\" 1 \"On\" ;\n");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIoTwinDbcParseTest, "IoTwin.Schematic.Dbc.ParseAndDescribe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIoTwinDbcParseTest::RunTest(const FString& Parameters)
{
	FCanDatabase Database;
	FString Error;
	TestTrue(TEXT("parses"), Database.LoadFromString(TestDbc, Error));
	TestEqual(TEXT("multi-line comment is skipped"), Database.NumMessages(), 2);

	const FCanMessageDef* Lamp = Database.FindMessage(0x100, false);
	TestNotNull(TEXT("LAMP_CMD found"), Lamp);
	if (Lamp)
	{
		TestEqual(TEXT("transmitter"), Lamp->Transmitter, FString(TEXT("LightECU")));
		TestEqual(TEXT("one signal"), Lamp->Signals.Num(), 1);
	}
	TestNull(TEXT("standard 0x200 is not the extended message"), Database.FindMessage(0x200, false));

	TestEqual(TEXT("value table"), Database.Describe(FCanFrame::MakeData(0x100, {0x01})),
		FString(TEXT("LAMP_CMD LampOn=1 (On)")));
	TestEqual(TEXT("value table off"), Database.Describe(FCanFrame::MakeData(0x100, {0x00})),
		FString(TEXT("LAMP_CMD LampOn=0 (Off)")));
	// Motorola, signed 12 bit: raw 0xFFF = -1 -> -1 * 0.5 - 40
	TestEqual(TEXT("motorola signed"), Database.Describe(FCanFrame::MakeData(0x200, {0xFF, 0xF0}, true)),
		FString(TEXT("SENSOR Temp=-40.5 degC")));
	// raw 0x010 = 16 -> 16 * 0.5 - 40
	TestEqual(TEXT("motorola positive"), Database.Describe(FCanFrame::MakeData(0x200, {0x01, 0x00}, true)),
		FString(TEXT("SENSOR Temp=-32 degC")));
	TestTrue(TEXT("unknown id"), Database.Describe(FCanFrame::MakeData(0x7FF, {0x00})).IsEmpty());

	FCanDatabase Broken;
	TestFalse(TEXT("signal without message"), Broken.LoadFromString(TEXT(" SG_ X : 0|1@1+ (1,0) [0|1] \"\" A\n"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIoTwinSystemDescriptionTest, "IoTwin.Schematic.System.Load",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIoTwinSystemDescriptionTest::RunTest(const FString& Parameters)
{
	const TCHAR* Json = TEXT(R"({
		"id": "t", "name": "Test",
		"nodes": [ { "id": "a", "kind": "virtual", "label": "A" }, { "id": "b", "kind": "physical" } ],
		"buses": [ { "id": "can0", "type": "can", "bitrate": 250000, "dbc": "../dbc/x.dbc", "members": ["a", "b"] } ],
		"links": [ { "id": "l", "type": "usb", "from": "b:USB", "to": "a" } ],
		"messages": [ { "name": "M", "producers": ["a"], "consumers": ["b"] } ]
	})");

	FSystemDescription System;
	FString Error;
	TestTrue(TEXT("loads"), System.LoadFromString(Json, TEXT("C:/data/systems"), Error));
	TestEqual(TEXT("nodes"), System.Nodes.Num(), 2);
	TestEqual(TEXT("bitrate"), System.Buses.Num() == 1 ? System.Buses[0].Bitrate : 0, 250000);
	TestEqual(TEXT("members"), System.Buses.Num() == 1 ? System.Buses[0].Members.Num() : 0, 2);
	TestEqual(TEXT("link endpoint node"), FSystemLink::NodeOf(TEXT("b:USB")), FString(TEXT("b")));
	TestEqual(TEXT("plain endpoint"), FSystemLink::NodeOf(TEXT("a")), FString(TEXT("a")));
	TestEqual(TEXT("relative path"), System.ResolvePath(TEXT("../dbc/x.dbc")), FString(TEXT("C:/data/dbc/x.dbc")));
	TestNotNull(TEXT("find message"), System.FindMessage(TEXT("M")));
	TestNotNull(TEXT("find node"), System.FindNode(TEXT("b")));

	TestFalse(TEXT("invalid JSON"), System.LoadFromString(TEXT("not json"), TEXT(""), Error));
	TestFalse(TEXT("no nodes"), System.LoadFromString(TEXT("{}"), TEXT(""), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIoTwinComponentCatalogTest, "IoTwin.Schematic.Components.Format",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIoTwinComponentCatalogTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("MHz"), FComponentCatalog::FormatQuantity(64000000.0, TEXT("Hz")), FString(TEXT("64 MHz")));
	TestEqual(TEXT("KiB"), FComponentCatalog::FormatQuantity(524288.0, TEXT("B")), FString(TEXT("512 KiB")));
	TestEqual(TEXT("mA"), FComponentCatalog::FormatQuantity(0.025, TEXT("A")), FString(TEXT("25 mA")));
	TestEqual(TEXT("uA"), FComponentCatalog::FormatQuantity(0.000002, TEXT("A")), FString(TEXT("2 \u00B5A")));
	TestEqual(TEXT("V"), FComponentCatalog::FormatQuantity(5.0, TEXT("V")), FString(TEXT("5 V")));
	TestEqual(TEXT("range"), FComponentCatalog::FormatParameter(0, false, 1.7, true, 0, false, 3.6, true, TEXT("V")),
		FString(TEXT("1.7 .. 3.6 V")));
	TestEqual(TEXT("negative range"), FComponentCatalog::FormatParameter(0, false, -40, true, 0, false, 85, true, TEXT("\u00B0C")),
		FString(TEXT("-40 .. 85 \u00B0C")));
	TestEqual(TEXT("max only"), FComponentCatalog::FormatParameter(0, false, 0, false, 0, false, 64e6, true, TEXT("Hz")),
		FString(TEXT("max 64 MHz")));
	TestEqual(TEXT("unknown"), FComponentCatalog::FormatParameter(0, false, 0, false, 0, false, 0, false, TEXT("A")),
		FString(TEXT("? A")));

	FComponentCatalog Catalog;
	FString Error;
	TestTrue(TEXT("component"), Catalog.AddFromString(TEXT(R"({
		"id": "x.part", "name": "Part", "manufacturer": "X",
		"parameters": {
			"clock": { "max": 16000000, "unit": "Hz", "verified": true },
			"current": { "typ": null, "unit": "A", "verified": false }
		},
		"contains": [ { "instance": "mcu", "component": "x.mcu" } ]
	})"), Error));
	const FComponentInfo* Part = Catalog.Find(TEXT("x.part"));
	TestNotNull(TEXT("found"), Part);
	if (Part && Part->Parameters.Num() == 2)
	{
		TestEqual(TEXT("order kept"), Part->Parameters[0].Name, FString(TEXT("clock")));
		TestEqual(TEXT("display"), Part->Parameters[0].Display, FString(TEXT("max 16 MHz")));
		TestTrue(TEXT("verified"), Part->Parameters[0].bVerified);
		TestEqual(TEXT("null value"), Part->Parameters[1].Display, FString(TEXT("? A")));
		TestFalse(TEXT("unverified"), Part->Parameters[1].bVerified);
		TestEqual(TEXT("contains"), Part->Contains.Num(), 1);
	}
	else
	{
		AddError(TEXT("expected two parameters"));
	}
	TestFalse(TEXT("missing id"), Catalog.AddFromString(TEXT("{ \"name\": \"no id\" }"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIoTwinSchematicRepositoryDataTest, "IoTwin.Schematic.Subsystem.RepositoryData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIoTwinSchematicRepositoryDataTest::RunTest(const FString& Parameters)
{
	FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("data"), TEXT("systems"), TEXT("light-demo.json")));
	FPaths::CollapseRelativeDirectories(Path);
	if (!FPaths::FileExists(Path))
	{
		AddWarning(FString::Printf(TEXT("%s not found (packaged or moved project), skipped"), *Path));
		return true;
	}

	// Subsystems must live inside a game instance (ClassWithin); this one is never initialised.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UIoTwinSchematicSubsystem* Schematic = NewObject<UIoTwinSchematicSubsystem>(GameInstance);
	TestTrue(TEXT("loads light-demo"), Schematic->LoadSystem(Path));
	TestNotNull(TEXT("live CAN bus"), Schematic->GetLiveBus());
	TestNotNull(TEXT("board in component database"), Schematic->GetComponents().Find(TEXT("st.nucleo-g0b1re")));
	TestNotNull(TEXT("MCU in component database"), Schematic->GetComponents().Find(TEXT("st.stm32g0b1ret6")));

	FString MessageName;
	TestEqual(TEXT("DBC decode"), Schematic->Describe(FCanFrame::MakeData(0x100, {0x01}), &MessageName),
		FString(TEXT("LAMP_CMD LampOn=1 (On)")));
	TestEqual(TEXT("message name"), MessageName, FString(TEXT("LAMP_CMD")));

	// Producer attribution: received SWITCH_STATE comes from the gateway, sent ones from the visualizer.
	TestEqual(TEXT("received switch"), Schematic->GuessProducer(TEXT("SWITCH_STATE"), false), FString(TEXT("gateway")));
	TestEqual(TEXT("sent switch"), Schematic->GuessProducer(TEXT("SWITCH_STATE"), true), FString(TEXT("visualizer")));
	TestEqual(TEXT("lamp command"), Schematic->GuessProducer(TEXT("LAMP_CMD"), false), FString(TEXT("ecu")));
	TestEqual(TEXT("unknown message"), Schematic->GuessProducer(TEXT(""), false), FString());
	const TArray<FString> ExpectedConsumers = {TEXT("visualizer"), TEXT("gateway")};
	TestTrue(TEXT("consumers"), Schematic->GetConsumers(TEXT("LAMP_CMD"), TEXT("ecu")) == ExpectedConsumers);

	Schematic->RecordFrame(FCanFrame::MakeData(0x100, {0x01}), false, 100.0);
	Schematic->RecordFrame(FCanFrame::MakeData(0x200, {0x01}), false, 100.5);
	Schematic->RecordFrame(FCanFrame::MakeData(0x7FF, {0x00}), false, 101.0);
	TestEqual(TEXT("total"), Schematic->GetTotalFrames(), 3);
	TestEqual(TEXT("log"), Schematic->GetLog().Num(), 3);
	const FSchematicNodeActivity* Ecu = Schematic->FindActivity(TEXT("ecu"));
	TestTrue(TEXT("ecu activity"), Ecu && Ecu->FrameCount == 1 && Ecu->LastMessage == TEXT("LAMP_CMD LampOn=1 (On)"));
	const FSchematicNodeActivity* Gateway = Schematic->FindActivity(TEXT("gateway"));
	TestTrue(TEXT("gateway activity"), Gateway && Gateway->FrameCount == 1);
	TestTrue(TEXT("unknown frame is logged unattributed"), Schematic->GetLog().Last().NodeId.IsEmpty());
	TestEqual(TEXT("pulses for attributed frames"), Schematic->GetPulses().Num(), 2);

	for (int32 Index = 0; Index < 40; ++Index)
	{
		Schematic->RecordFrame(FCanFrame::MakeData(0x100, {0x00}), false, 102.0 + Index * 0.01);
	}
	TestEqual(TEXT("log is bounded"), Schematic->GetLog().Num(), UIoTwinSchematicSubsystem::MaxLogEntries);
	TestTrue(TEXT("pulses are bounded"), Schematic->GetPulses().Num() <= UIoTwinSchematicSubsystem::MaxPulses);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
