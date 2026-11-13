#include <catch2/catch_all.hpp>

#include <string>

#include "dbc.h"

namespace {

const char* kSample = R"(VERSION ""

NS_ :
  CM_
  BA_DEF_

BS_:

BU_: ECU Tester

BO_ 291 ENGINE: 8 ECU
 SG_ Speed : 0|16@1+ (0.25,0) [0|16383.75] "rpm" Tester
 SG_ Temp : 23|8@0- (1,-40) [-168|87] "degC" Tester
 SG_ Mode : 32|2@1+ (1,0) [0|3] "" Tester

BO_ 2566844926 EXT_STATUS: 4 ECU
 SG_ Mux M : 0|8@1+ (1,0) [0|255] "" Tester
 SG_ ValueA m1 : 8|16@1+ (1,0) [0|65535] "" Tester
 SG_ ValueB m2 : 8|8@1- (0.5,0) [-64|63.5] "V" Tester

CM_ BO_ 291 "A comment
spanning lines with BO_ 999 FAKE: 8 X inside";
BA_DEF_ SG_ "GenSigStartValue" INT 0 100;
VAL_ 291 Mode 0 "Off" 1 "Idle" 2 "Run" 3 "Fault" ;
)";

struct Database {
  DbcDatabase db{};
  char error[128] = {};
  explicit Database(const char* text) { ok = dbc_parse(text, &db, error, sizeof(error)); }
  ~Database() { dbc_free(&db); }
  bool ok = false;
};

}  // namespace

TEST_CASE("DBC parser reads messages and signals", "[dbc]") {
  Database d(kSample);
  INFO(d.error);
  REQUIRE(d.ok);
  REQUIRE(d.db.message_count == 2);  // the BO_ inside the comment is skipped

  const DbcMessage* engine = dbc_find_message(&d.db, 0x123, false);
  REQUIRE(engine != nullptr);
  CHECK(std::string(engine->name) == "ENGINE");
  CHECK(engine->dlc == 8);
  CHECK(std::string(engine->transmitter) == "ECU");
  REQUIRE(engine->signal_count == 3);

  const DbcSignal* speed = dbc_find_signal(engine, "Speed");
  REQUIRE(speed != nullptr);
  CHECK(speed->signal.byte_order == CAN_BYTE_ORDER_INTEL);
  CHECK(speed->signal.factor == Catch::Approx(0.25));
  CHECK(std::string(speed->unit) == "rpm");

  const DbcSignal* temp = dbc_find_signal(engine, "Temp");
  REQUIRE(temp != nullptr);
  CHECK(temp->signal.byte_order == CAN_BYTE_ORDER_MOTOROLA);
  CHECK(temp->signal.is_signed);
  CHECK(temp->minimum == Catch::Approx(-168));

  const DbcSignal* mode = dbc_find_signal(engine, "Mode");
  REQUIRE(mode != nullptr);
  CHECK(std::string(dbc_value_name(mode, 2)) == "Run");
  CHECK(dbc_value_name(mode, 7) == nullptr);
}

TEST_CASE("DBC parser handles extended ids and multiplexing", "[dbc]") {
  Database d(kSample);
  REQUIRE(d.ok);
  const DbcMessage* ext = dbc_find_message(&d.db, 0x18FEF1FE, true);
  REQUIRE(ext != nullptr);
  CHECK(dbc_find_message(&d.db, 0x18FEF1FE, false) == nullptr);
  CHECK(dbc_find_signal(ext, "Mux")->is_multiplexer);
  CHECK(dbc_find_signal(ext, "ValueB")->mux_value == 2);

  CanFrame frame{};
  frame.id = 0x18FEF1FE;
  frame.extended = true;
  frame.dlc = 4;
  frame.data[0] = 2;     // multiplexer selects ValueB
  frame.data[1] = 0xF6;  // -10 * 0.5 = -5 V
  char text[128];
  REQUIRE(dbc_describe_frame(&d.db, &frame, text, sizeof(text)) > 0);
  CHECK(std::string(text) == "EXT_STATUS Mux=2 ValueB=-5 V");
}

TEST_CASE("DBC describes and builds frames", "[dbc]") {
  Database d(kSample);
  REQUIRE(d.ok);
  const DbcMessage* engine = dbc_find_message_by_name(&d.db, "ENGINE");
  REQUIRE(engine != nullptr);

  CanFrame frame{};
  REQUIRE(dbc_set_signal(engine, "Speed", 1500.0, &frame));
  REQUIRE(dbc_set_signal(engine, "Temp", 85.0, &frame));
  REQUIRE(dbc_set_signal(engine, "Mode", 2, &frame));
  CHECK_FALSE(dbc_set_signal(engine, "Mode", 4, &frame));      // out of range
  CHECK_FALSE(dbc_set_signal(engine, "Missing", 1, &frame));
  CHECK(frame.id == 0x123);
  CHECK(frame.dlc == 8);
  CHECK(frame.data[0] == 0x70);  // 6000 = 0x1770 little endian
  CHECK(frame.data[1] == 0x17);
  CHECK(frame.data[2] == 125);   // 85 + 40, Motorola byte 2

  char text[160];
  REQUIRE(dbc_describe_frame(&d.db, &frame, text, sizeof(text)) > 0);
  CHECK(std::string(text) == "ENGINE Speed=1500 rpm Temp=85 degC Mode=2 (Run)");

  CanFrame unknown{};
  unknown.id = 0x7FF;
  CHECK(dbc_describe_frame(&d.db, &unknown, text, sizeof(text)) == -1);
}

TEST_CASE("DBC parser reports errors with line numbers", "[dbc]") {
  {
    Database d("BO_ 100 OK: 8 ECU\n SG_ Broken : 0|8@2+ (1,0) [0|1] \"\" X\n");
    CHECK_FALSE(d.ok);
    CHECK(std::string(d.error).rfind("line 2:", 0) == 0);
  }
  {
    Database d(" SG_ Orphan : 0|8@1+ (1,0) [0|1] \"\" X\n");
    CHECK_FALSE(d.ok);
  }
  {
    Database d("BO_ 100 BIG: 8 ECU\n SG_ TooLong : 60|8@1+ (1,0) [0|1] \"\" X\n");
    CHECK_FALSE(d.ok);
  }
}

TEST_CASE("The IoTwin demo DBC describes the light demo", "[dbc]") {
  DbcDatabase db{};
  char error[128] = {};
  REQUIRE(dbc_load_file(IOTWIN_DBC_PATH, &db, error, sizeof(error)));
  CanFrame frame{};
  frame.id = 0x100;
  frame.dlc = 2;
  frame.data[0] = 1;
  frame.data[1] = 60;
  char text[160];
  REQUIRE(dbc_describe_frame(&db, &frame, text, sizeof(text)) > 0);
  CHECK(std::string(text) == "LAMP_CMD LampOn=1 (On) LampLevel=60 %");
  frame.id = 0x200;
  frame.dlc = 1;
  frame.data[0] = 0;
  REQUIRE(dbc_describe_frame(&db, &frame, text, sizeof(text)) > 0);
  CHECK(std::string(text) == "SWITCH_STATE SwitchOn=0 (Off)");
  dbc_free(&db);
}
