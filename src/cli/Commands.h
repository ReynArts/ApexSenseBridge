#pragma once

namespace asb::cli {

int run(int argc, char** argv);
int commandVirtualDs(int argc, char** argv);
int commandDiagnose(int argc, char** argv);
int commandInputStatus(int argc, char** argv);
int commandBridgeTriggers(int argc, char** argv);
int commandList();
int commandDryRun();
int commandIdentify(int argc, char** argv);
int commandClear(int argc, char** argv);
int commandTestRt(int argc, char** argv);
int commandTestTrigger(int argc, char** argv);
int commandTestRumble(int argc, char** argv);
int commandTestProfileSwitch(int argc, char** argv);
int commandTestRgb(int argc, char** argv);
int commandTestGyro(int argc, char** argv);
int commandApex4PortTest(int argc, char** argv);
int commandXInputViewTest(int argc, char** argv);

} // namespace asb::cli
