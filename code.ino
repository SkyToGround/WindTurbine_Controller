#include <U8g2lib.h>
#include <EEPROM.h>

class TurbineLogic {
public:
	TurbineLogic() {}
	bool isTurbineOn() { return TurbineOn;}
	void determineOnState(int start_rpm, int stop_rpm, int current_rpm) {
		unsigned int CurrentTime = millis();
		bool DeterminedState = current_rpm >= start_rpm an current_rpm <= start_rpm;
		if (DeterminedState != TurbineOn and CurrentTime < LastStateChangeTime + StateChangeWaitMS) {
			return;
		}
		TurbineOn = DeterminedState;
		LastStateChangeTime = CurrentTime;
	}
private:
	bool TurbineOn = false;
	const unsigned int StateChangeWaitMS = 5000;
	unsigned int LastStateChangeTime = 0;
};

class RPMMeasurement {
public:
	RPMMeasurement() {}
	void newPeriod(unsigned int StartTimeMS) {
		FirstPulseTimeMS = StartTimeMS;
		LastPulseTimeMS = StartTimeMS;
		RegisteredPulses = 1;
	}
	void registerPulse() {
		unsigned int CurrentTimeMS = millis();
		if (CurrentTimeMS <  FirstPulseTimeMS) {
			newPeriod(CurrentTimeMS);
			return;
		}
		if (CurrentTimeMS < LastPulseTimeMS + DeBounceTimeMS) {
			return;
		}
		if (CurrentTimeMS > FirstPulseTimeMS + AveragingTimeMS) {
			if (RegisteredPulses >= 1) {
				CurrentRPM = 60000 / ((CurrentTimeMS - FirstPulseTimeMS) / RegisteredPulses);
			} else {
				CurrentRPM = 0;
			}
			newPeriod(CurrentTimeMS);
			return;
		}
		
		LastPulseTimeMS = CurrentTimeMS;
		RegisteredPulses++;
	}
	void doRPMCalc() {
		unsigned int CurrentTimeMS = millis();
		noInterrupts();
		if (CurrentTimeMS > LastPulseTimeMS + AveragingTimeMS) {
			if (RegisteredPulses > 2) {
				CurrentRPM = 60000 / ((LastPulseTimeMS - FirstPulseTimeMS) / RegisteredPulses);
			} else {
				CurrentRPM = 0;
			}
			FirstPulseTimeMS = 0;
			LastPulseTimeMS = 0;
			RegisteredPulses = 0
		}
		interrupts();
	}
	unsigned int getRPM() { return CurrentRPM; }
private:
	unsigned int AveragingTimeMS = 500;
	unsigned int FirstPulseTimeMS = 0;
	unsigned int LastPulseTimeMS = 0;
	unsigned int RegisteredPulses = 0;
	unsigned int CurrentRPM = 0;
	unsigned int const DeBounceTimeMS = 5;
};

U8G2_SSD1306_128X64_VCOMH0_1_4W_HW_SPI u8g2(U8G2_R2, U8X8_PIN_NONE, 8, 9);

char buffer[40];
int start_rpm = 1500;
const int start_rpm_addr = 0;
const int stop_rpm_addr = 2;
int stop_rpm = 1600;
const int lower_rpm_limit = 1500;
const int upper_rpm_limit = 1550;
int selected_row = 0;
unsigned int button_time = 0;
const int button_timeout = 100;
const int RELAY_PIN = 4;
RPMMeasurement rpm_calc;
TurbineLogic logic;

void writeIntToEEPROM(int address, int number)
{ 
  byte byte1 = number >> 8;
  byte byte2 = number & 0xFF;
  EEPROM.write(address, byte1);
  EEPROM.write(address + 1, byte2);
}

int readIntFromEEPROM(int address)
{
  byte byte1 = EEPROM.read(address);
  byte byte2 = EEPROM.read(address + 1);
  return (byte1 << 8) + byte2;
}

int get_rpm_from_eeprom(int addr) {
  int stored_value = readIntFromEEPROM(addr);
  if (stored_value < lower_rpm_limit) {
    return lower_rpm_limit;
  }
  if (stored_value > upper_rpm_limit) {
    return upper_rpm_limit - 1;
  }
  return stored_value;
}

void on_interrupt() {
	rpm_calc.registerPulse();
}

void setup() {
  pinMode(2, INPUT);
  pinMode(3, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP);
  u8g2.initDisplay();
  u8g2.setPowerSave(0);
  Serial.begin(9600);
  start_rpm = get_rpm_from_eeprom(start_rpm_addr);
  stop_rpm = get_rpm_from_eeprom(stop_rpm_addr);
  pinMode(1, INPUT);
  attachInterrupt(digitalPinToInterrupt(1), ISR, FALLING);
}

void render_line(int line, char *format_string, int value, bool inverted) {
  int y_value = 14 + line * 16;
  if (inverted) {
    u8g2.drawBox(0, y_value - 14, 128, 16);
    u8g2.setColorIndex(0);
  }
  sprintf(buffer, format_string, value);
  u8g2.drawStr(0,y_value, buffer);
  if (inverted) {
    u8g2.setColorIndex(1);
  }
}

void render_text(int current_rpm, int start_rpm, int stop_rpm, bool on, int marked_row) {
  bool invert_start = false;
  bool invert_stop = false;
  switch (marked_row) {
    case 0:
      invert_start = true;
      break;
    case 1:
      invert_stop = true;
      break;
  }
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_helvR14_tf);
    render_line(0, "Varv: %d/m", current_rpm, false);
    render_line(1, "Start: %d/m", start_rpm, invert_start);
    render_line(2, "Stop: %d/m", stop_rpm, invert_stop);
    if (on) {
      sprintf(buffer, "På: %s", "Ja");
    } else {
      sprintf(buffer, "På: %s", "Nej");
    }
    u8g2.drawUTF8(0,61, buffer);
  } while ( u8g2.nextPage() );
}

bool button_is_pressed(int button) {
  unsigned int c_time = millis();
  if (c_time < button_time) {
    button_time = 0;
  }
  if (c_time < button_time + button_timeout) {
    return false;
  }
  if (not digitalRead(button)) {
    button_time = c_time;
    Serial.print("Button pressed!\n");
    return true; 
  }
  return false;
}

void decrease_start_rpm() {
  if (start_rpm <= lower_rpm_limit) {
    return;
  }
  start_rpm -= 1;
}

void increase_start_rpm() {
  if (start_rpm == upper_rpm_limit or start_rpm + 1 >= stop_rpm) {
    return;
  }
  start_rpm += 1;
}

void decrease_stop_rpm() {
  if (stop_rpm <= start_rpm or stop_rpm <= lower_rpm_limit) {
    return;
  }
  stop_rpm -= 1;
}

void increase_stop_rpm() {
  if (stop_rpm >= upper_rpm_limit) {
    return;
  }
  stop_rpm += 1;
}

void loop() {
	rpm_calc.doRPMCalc();
	logic.determineOnState(start_rpm, stop_rpm, rpm_calc.getRPM());
	
  render_text(rpm_calc.getRPM(), start_rpm, stop_rpm, logic.isTurbineOn(), selected_row);
  if (button_is_pressed(2)) {
    selected_row = selected_row ^ 0x01;
  }
  if (button_is_pressed(3)) {
    switch (selected_row) {
      case 0:
        decrease_start_rpm();
        break;
      case 1:
        decrease_stop_rpm();
        break;
    }
  } else if (button_is_pressed(4)) {
    switch (selected_row) {
      case 0:
        increase_start_rpm();
        break;
      case 1:
        increase_stop_rpm();
        break;
    }
  }
}
