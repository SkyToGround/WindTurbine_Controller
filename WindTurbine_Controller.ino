#include <U8g2lib.h>
#include <EEPROM.h>
#include <math.h>

class TurbineLogic {
public:
	TurbineLogic() {}
	bool isTurbineOn() { return TurbineOn;}
	void determineOnState(int start_rpm, int stop_rpm, int current_rpm) {
		uint32_t CurrentTime = millis();
    bool DeterminedState = current_rpm >= start_rpm or (TurbineOn and current_rpm >= stop_rpm);
		if (DeterminedState != TurbineOn and CurrentTime < LastStateChangeTime + StateChangeWaitMS) {
			return;
		}
		TurbineOn = DeterminedState;
		LastStateChangeTime = CurrentTime;
	}
private:
	bool TurbineOn = false;
	const uint32_t StateChangeWaitMS = 5000;
	uint32_t LastStateChangeTime = 0;
};

class RPMMeasurement {
public:
	RPMMeasurement() {}
	void newPeriod(uint32_t StartTimeMS) {
		FirstPulseTimeMS = StartTimeMS;
		LastPulseTimeMS = StartTimeMS;
		RegisteredPulses = 0;
	}
	void registerPulse() {
		uint32_t CurrentTimeMS = millis();
		if (CurrentTimeMS <  FirstPulseTimeMS) {
			newPeriod(CurrentTimeMS);
			return;
		}
		if (CurrentTimeMS < LastPulseTimeMS + DeBounceTimeMS) {
			return;
		}
		if (CurrentTimeMS > FirstPulseTimeMS + AveragingTimeMS) {
			if (RegisteredPulses >= 1) {
				calcRPM();
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
    uint32_t CurrentTimeMS = millis();
		noInterrupts();
		if (CurrentTimeMS > LastPulseTimeMS + AveragingTimeMS) {
			if (RegisteredPulses > 2) {
				calcRPM();
			} else {
				CurrentRPM = 0;
			}
			FirstPulseTimeMS = 0;
			LastPulseTimeMS = 0;
			RegisteredPulses = 0;
		}
		interrupts();
	}
	unsigned int getRPM() { return CurrentRPM; }
private:
  void calcRPM() {
    CurrentRPM = round(60000.0 / ((LastPulseTimeMS - FirstPulseTimeMS) / float(RegisteredPulses)));
  }
	uint32_t AveragingTimeMS = 1000;
	uint32_t FirstPulseTimeMS = 0;
	uint32_t LastPulseTimeMS = 0;
	unsigned int RegisteredPulses = 0;
	unsigned int CurrentRPM = 0;
	unsigned int const DeBounceTimeMS = 5;
};

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

class RPMSettingsTracker {
  public:
    RPMSettingsTracker() {
      c_start_rpm = readIntFromEEPROM(start_rpm_addr);
      if (c_start_rpm <= lower_rpm_limit or c_start_rpm > upper_rpm_limit) {
        c_start_rpm = upper_rpm_limit;
      }
      c_stop_rpm = readIntFromEEPROM(stop_rpm_addr);
      if (c_stop_rpm < lower_rpm_limit or c_stop_rpm + 1 >= c_start_rpm) {
        c_stop_rpm = lower_rpm_limit;
      }
    }

    int start_rpm() {
      return c_start_rpm;
    }

    int stop_rpm() {
      return c_stop_rpm;
    }

    void saveRPMSettings() {
      if (WaitingForSave and millis() > LastRPMChangeTimestamp + SaveRPMTimeoutMS) {
        WaitingForSave = false;
        writeIntToEEPROM(start_rpm_addr, c_start_rpm);
        writeIntToEEPROM(stop_rpm_addr, c_stop_rpm);
      }
    }

    void decrease_start_rpm() {
      if (c_start_rpm - 1 <= c_stop_rpm) {
        return;
      }
      c_start_rpm -= 1;
      start_save_timer();
    }

    void increase_start_rpm() {
      if (c_start_rpm == upper_rpm_limit) {
        return;
      }
      c_start_rpm += 1;
      start_save_timer();
    }

    void decrease_stop_rpm() {
      if (c_stop_rpm <= upper_rpm_limit) {
        return;
      }
      c_stop_rpm -= 1;
      start_save_timer();
    }

    void increase_stop_rpm() {
      if (c_stop_rpm + 1 >= c_start_rpm) {
        return;
      }
      c_stop_rpm += 1;
      start_save_timer();
    }
  private:
    void start_save_timer() {
      WaitingForSave = true;
      LastRPMChangeTimestamp = millis();
    }
    int c_start_rpm;
    int c_stop_rpm;

    const int start_rpm_addr = 0;
    const int stop_rpm_addr = 2;
    const int lower_rpm_limit = 1400;
    const int upper_rpm_limit = 1600;
    bool WaitingForSave = false;
    uint32_t LastRPMChangeTimestamp = 0;
    const uint32_t SaveRPMTimeoutMS = 5000;
};

U8G2_SSD1306_128X64_VCOMH0_1_4W_HW_SPI u8g2(U8G2_R2, U8X8_PIN_NONE, 8, 9);

char buffer[40];


int selected_row = 0;
uint32_t button_time = 0;
const uint32_t button_timeout = 150;
const int RELAY_PIN = 4;
const int RPM_PIN = 2;
const int SELECT_BTN_PIN = 4;
const int INCR_BTN_PIN = 5;
const int DECR_BTN_PIN = 6;
RPMMeasurement rpm_calc;
TurbineLogic logic;
RPMSettingsTracker RPMSettings;

void on_interrupt() {
	rpm_calc.registerPulse();
}

void setup() {
  pinMode(SELECT_BTN_PIN, INPUT);
  pinMode(INCR_BTN_PIN, INPUT);
  pinMode(DECR_BTN_PIN, INPUT);
  u8g2.setBusClock(8000000);
  u8g2.initDisplay();
  u8g2.setPowerSave(0);
  Serial.begin(115200);
  pinMode(RPM_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RPM_PIN), on_interrupt, FALLING);
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
  uint32_t c_time = millis();
  if (c_time < button_time) {
    button_time = 0;
  }
  if (c_time < button_time + button_timeout) {
    return false;
  }
  if (not digitalRead(button)) {
    button_time = c_time;
    return true; 
  }
  return false;
}

void loop() {
	rpm_calc.doRPMCalc();
	logic.determineOnState(RPMSettings.start_rpm(), RPMSettings.stop_rpm(), rpm_calc.getRPM());
  render_text(rpm_calc.getRPM(), RPMSettings.start_rpm(), RPMSettings.stop_rpm(), logic.isTurbineOn(), selected_row);
  if (button_is_pressed(SELECT_BTN_PIN)) {
    selected_row = selected_row ^ 0x01;
  }
  if (button_is_pressed(INCR_BTN_PIN)) {
    switch (selected_row) {
      case 0:
        RPMSettings.decrease_start_rpm();
        break;
      case 1:
        RPMSettings.decrease_stop_rpm();
        break;
    }
  } else if (button_is_pressed(DECR_BTN_PIN)) {
    switch (selected_row) {
      case 0:
        RPMSettings.increase_start_rpm();
        break;
      case 1:
        RPMSettings.increase_stop_rpm();
        break;
    }
  }
  RPMSettings.saveRPMSettings();
}
