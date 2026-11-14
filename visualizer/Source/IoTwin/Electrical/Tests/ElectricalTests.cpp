// IoTwin - automation tests for the electrical simulation in the visualizer: the
// C core compiled with MSVC, the model builder and the session (ADR 0008).
// Run: UnrealEditor-Cmd IoTwin.uproject -ExecCmds="Automation RunTests IoTwin;Quit" -unattended -nullrhi

#include "Electrical/ElectricalModelBuilder.h"
#include "Misc/AutomationTest.h"
#include "Systems/ElectricalPortComponent.h"
#include "UObject/Package.h"

#include "el_session.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIoTwinElectricalIdTest, "IoTwin.Electrical.Builder.Ids",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIoTwinElectricalIdTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("allowed characters stay"), FElectricalModelBuilder::SanitizeId(TEXT("Lamp_1.Port-A")), FString(TEXT("Lamp_1.Port-A")));
	TestEqual(TEXT("colon and space are replaced"), FElectricalModelBuilder::SanitizeId(TEXT("a:b c")), FString(TEXT("a_b_c")));
	TestEqual(TEXT("shortened to the core's id length"), FElectricalModelBuilder::SanitizeId(FString::ChrN(80, 'x')).Len(), EL_ID_MAX - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIoTwinElectricalSessionTest, "IoTwin.Electrical.Session.DimAndShortCircuit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIoTwinElectricalSessionTest::RunTest(const FString& Parameters)
{
	// supply -> F1 B10 -> relay (dimmer) -> port A ==cable 1.5 mm^2, 5 m== port B -> lamp 60 W
	UElectricalPortComponent* PortA = NewObject<UElectricalPortComponent>(GetTransientPackage(), TEXT("PortA"));
	UElectricalPortComponent* PortB = NewObject<UElectricalPortComponent>(GetTransientPackage(), TEXT("PortB"));
	ElModel Model;
	el_model_init(&Model);
	FElectricalModelBuilder Builder(Model);
	const FString A = Builder.AddPort(PortA);
	const FString B = Builder.AddPort(PortB);
	TestEqual(TEXT("port ids"), A + TEXT(" ") + B, FString(TEXT("PortA PortB")));
	// Refs stay valid while more devices are added (the device array grows and may move).
	const FElectricalDeviceRef Breaker = Builder.AddDevice(TEXT("F1"), EL_BREAKER);
	const FElectricalDeviceRef Relay = Builder.AddDevice(TEXT("relay"), EL_DIMMER);
	const FElectricalDeviceRef Lamp = Builder.AddDevice(TEXT("lamp"), EL_LOAD);
	TestTrue(TEXT("supply"), static_cast<bool>(Builder.AddDevice(TEXT("supply"), EL_SUPPLY)));
	TestFalse(TEXT("duplicate id refused"), static_cast<bool>(Builder.AddDevice(TEXT("lamp"), EL_LOAD)));
	for (int32 i = 0; i < 40; ++i)
	{
		Builder.AddDevice(FString::Printf(TEXT("spare%d"), i), EL_JUNCTION);
	}
	if (!Breaker || !Relay || !Lamp)
	{
		el_model_free(&Model);
		return false;
	}
	Breaker->rated_current_a = 10.0;
	Relay->on = true;
	Lamp->rated_power_w = 60.0;
	const FString Cable = Builder.AddCable(TEXT("w1"), 1.5, 5.0);
	TestTrue(TEXT("wiring"), Builder.Connect(TEXT("supply:L1"), TEXT("F1:in"), TEXT("L")) &&
		Builder.Connect(TEXT("F1:out"), TEXT("relay:in"), TEXT("L")) &&
		Builder.Connect(TEXT("relay:out"), A + TEXT(":L"), TEXT("L")) &&
		Builder.Connect(TEXT("supply:N"), A + TEXT(":N"), TEXT("N")) &&
		Builder.Connect(TEXT("supply:PE"), A + TEXT(":PE"), TEXT("PE")) &&
		Builder.Connect(A + TEXT(":L"), B + TEXT(":L"), TEXT("L"), Cable) &&
		Builder.Connect(A + TEXT(":N"), B + TEXT(":N"), TEXT("N"), Cable) &&
		Builder.Connect(A + TEXT(":PE"), B + TEXT(":PE"), TEXT("PE"), Cable) &&
		Builder.ConnectToPort(TEXT("lamp"), PortB));
	TestEqual(TEXT("one failure (the duplicate)"), Builder.GetFailures(), 1);

	ElSession* Session = el_session_create(&Model);
	if (!TestNotNull(TEXT("session"), Session))
	{
		return false;
	}
	const int LampIndex = el_session_find_device(Session, "lamp");
	const int BreakerIndex = el_session_find_device(Session, "F1");

	el_session_step(Session, 0.01);
	const ElSnapshot* Snapshot = el_session_snapshot(Session);
	const double FullPower = Snapshot->devices[LampIndex].power_w;
	TestTrue(TEXT("lamp powered"), (Snapshot->devices[LampIndex].flags & EL_STATE_POWERED) != 0);
	TestTrue(TEXT("about 60 W"), FMath::IsNearlyEqual(FullPower, 60.0, 1.0));

	const ElCommand Dim = el_command(EL_CMD_SET_LEVEL, "relay", nullptr, 0.3);
	el_session_send(Session, &Dim);
	el_session_step(Session, 0.01);
	Snapshot = el_session_snapshot(Session);
	TestTrue(TEXT("dimmed to 30 % of the power"), FMath::IsNearlyEqual(Snapshot->devices[LampIndex].power_w, 0.3 * FullPower, 0.01 * FullPower));

	const FString Short = B + TEXT(":L");
	const FString Neutral = B + TEXT(":N");
	const ElCommand Full = el_command(EL_CMD_SET_LEVEL, "relay", nullptr, 1.0);
	const ElCommand Fault = el_command(EL_CMD_ADD_FAULT, TCHAR_TO_UTF8(*Short), TCHAR_TO_UTF8(*Neutral), 0.0);
	el_session_send(Session, &Full);
	el_session_send(Session, &Fault);
	el_session_step(Session, 0.01);
	Snapshot = el_session_snapshot(Session);
	TestTrue(TEXT("F1 tripped"), (Snapshot->devices[BreakerIndex].flags & EL_STATE_TRIPPED) != 0);
	TestTrue(TEXT("lamp dark"), (Snapshot->devices[LampIndex].flags & EL_STATE_POWERED) == 0);
	ElEvent Event;
	TestTrue(TEXT("trip event"), el_session_next_event(Session, &Event) &&
		FString(UTF8_TO_TCHAR(Event.text)).StartsWith(TEXT("F1 tripped (short circuit")));
	el_session_destroy(Session);
	return true;
}

#endif
