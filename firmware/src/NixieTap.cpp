#include <Arduino.h>
#include <nixie.h>
#include <NixieAPI.h>
#include <BQ32000RTC.h>
#include <NtpClientLib.h> // Development version is used: https://github.com/gmag11/NtpClient/tree/develop
#include <TimeLib.h>
#include <WiFiManager.h>  // Development version is used: https://github.com/tzapu/WiFiManager/tree/development
#include <EEPROM.h>
#include <Ticker.h>

#define DEBUG // When testing the code you should leave DEBUG defined if you want to see messages on the serial monitor.

ICACHE_RAM_ATTR void irq_1Hz_int();         // Interrupt function for changing the dot state every 1 second.
ICACHE_RAM_ATTR void tuchButtonPressed();   // Interrupt function when button is pressed.
ICACHE_RAM_ATTR void scrollDots();          // Interrupt function for scrolling dots.
void processSyncEvent(NTPSyncEvent_t ntpEvent);
void enableSecDot();
void disableSecDot();
void startPortalManually();    // Manually launch the WiFiMenager portal.
void updateParameters();
void readParameters();
void cryptoRefresh();
void weatherRefresh();
void updateTime();

volatile bool dot_state = LOW;
bool stopDef = false, secDotDef = false;
bool wifiFirstConnected = true;
bool syncEventTriggered = false; // True if a time event has been triggered.
int16_t timeZoneOffset = 0;
uint16_t yearInt = 0, currencyID = 0;
uint8_t monthInt = 0, dayInt = 0, hoursInt = 0, minutesInt = 0, timeFormat = 1, weatherFormat = 1, dst = 0, firstSyncEvent = 1;
uint8_t timeEnabled = 1, dateEnabled = 1, cryptoEnabled = 0, temperatureEnabled = 0, configButton = 0, currentMinutes = 0, prevMinutes = 0, prev_dot_state = 0;
uint8_t setTimeManuallyFlag = 0, setTimeSemiAutoFlag = 0, setTimeAutoFlag = 0, locationObtained = 0;
volatile uint8_t state = 0, dotPosition = 0b10, weatherRefreshFlag = 1, cryptoRefreshFlag = 1;
char WMTimeFormat[4] = "", WMYear[6] = "", WMMonth[4] = "", WMDay[4] = "", WMHours[4] = "", WMMinutes[4] = "", WMweatherFormat[4] = "", WMcurrencyID[6] = "", WMSSID[30] = "", WMPassword[30] = "";
char WMsetTimeManuallyFlag[4] = "", WMsetTimeSemiAutoFlag[4] = "", WMsetTimeAutoFlag[4] = "", WMTimeZoneOffset[8] = "", WMdst[4] = "";
char tzdbKey[50] = "", stackKey[50] = "", googleLKey[50] = "", googleTZkey[50] = "", weatherKey[50] = "", SSID[30] = "NixieTap", password[30] = "NixieTap";
char WMtimeEnable[4] = "", WMdateEnable[4] = "", WMcryptoEnable[4] = "", WMtempEnable[4] = "";
unsigned long previousMillis = 0;
String cryptoCurrencyPrice = "", temperature = "", loc = "";
Ticker movingDot, priceRefresh, temperatureRefresh; // Initializing software timer interrupt called movingDot and priceRefresh.
NTPSyncEvent_t ntpEvent;    // Last triggered event.
WiFiManager wifiManager;
time_t t;

// Initialization of parameters for manual configuration of time and date.
WiFiManagerParameter text0("<p><b>Select time adjustment methode: </b></p>");
WiFiManagerParameter setTimeManually("setTimeManuallyFlag", "Set time manually(1(YES)/0(NO): ", WMsetTimeManuallyFlag, 2);
WiFiManagerParameter setTimeSemiAuto("setTimeSemiAutoFlag", "Set time semi-auto(1(YES)/0(NO): ", WMsetTimeSemiAutoFlag, 2);
WiFiManagerParameter setTimeAuto("setTimeAutoFlag", "Set time automatically(1(YES)/0(NO): ", WMsetTimeAutoFlag, 2);
WiFiManagerParameter text1("<h1><center>Manual time adjustment</center></h1>");
WiFiManagerParameter text2("<p><b>Please fill in all fields with the current date and time: </b></p>");
WiFiManagerParameter yearWM("year", "Year: ", WMYear, 4);
WiFiManagerParameter monthWM("month", "Month: (1-January,..., 12-December)", WMMonth, 2);
WiFiManagerParameter dayWM("day", "Day: (From 1 to 31)", WMDay, 2);
WiFiManagerParameter hoursWM("hours", "Hours: (must be 24h format)", WMHours, 2);
WiFiManagerParameter minutesWM("minutes", "Minutes: ", WMMinutes, 2);
WiFiManagerParameter formatWM("timeFormat", "Time format(24h=1/12h=0): ", WMTimeFormat, 2);
WiFiManagerParameter text21("<h1><center>Semi-auto time adjustment</center></h1>");
WiFiManagerParameter text22("<p><b>Please enter your time zone offset in minutes and dst(Day Light Saving Time) condition: </b></p>");
WiFiManagerParameter timeZoneOffsetWM("timeZoneOffset", "Time zone offset in minutes: ", WMTimeZoneOffset, 8);
WiFiManagerParameter dstWM("dst", "Day light saving status 0(disable)/1(enable): ", WMdst, 2);
WiFiManagerParameter text3("<h1><center>Auto time adjustment</center></h1>");
WiFiManagerParameter text4("<p><b>Please fill in the field for which you have a key:</b></p>");
WiFiManagerParameter timezonedbKey("tzdbKey", "TimezoneDB API Key: ", tzdbKey, 50);
WiFiManagerParameter ipStackKey("stackKey", "IPstack API Key: ", stackKey, 50);
WiFiManagerParameter googleLocKey("googleLKey", "Google Geolocation API Key: ", googleLKey, 50);
WiFiManagerParameter googleTimeZoneKey("googleTZkey", "Google Time Zone API Key: ", googleTZkey, 50);
WiFiManagerParameter openWeatherMapKey("weatherKey", "Open Weather Map API Key: ", weatherKey, 50);
WiFiManagerParameter openWeatherMapFormat("weatherFormat", "Please select weather format(Metric=1/Imperial=0): ", WMweatherFormat, 2);
WiFiManagerParameter text5("<p><b>All entered parameters will be permanently saved until they are replaced with the new one.</b></p>");
WiFiManagerParameter text6("<h1><center>Configuration of crypto currency ID</center></h1>");
WiFiManagerParameter cryptoID("cryptoID", "Enter cryptocurrency ID: (Example: Bitcoin: 1, Litecoin: 2, Ethereum: 1027, Ripple: 52): ", WMcurrencyID, 6);
WiFiManagerParameter text7("<p>NixieTap uses the <b>CoinMarketCap</b> API service to get the price of the cryptocurrencies.</p>");
WiFiManagerParameter text8("<p>For a complete list of cryptocurrency IDs visit: <b>https://api.coinmarketcap.com/v2/listings/</b></p>");
WiFiManagerParameter text9("<h1><center>Set NixieTap password and SSID</center></h1>");
WiFiManagerParameter ssid("SSID", "Set new SSID: ", WMSSID, 30);
WiFiManagerParameter pass("Password", "Set new password: ", WMPassword, 30);
WiFiManagerParameter text10("<h1><center>Select displaying mode</center></h1>");
WiFiManagerParameter enableTime("timeEnable", "Enable time display: ", WMtimeEnable, 2);
WiFiManagerParameter enableDate("dateEnable", "Enable date display: ", WMdateEnable, 2);
WiFiManagerParameter enableCrypto("cryptoEnable", "Enable crypto display: ", WMcryptoEnable, 2);
WiFiManagerParameter enableTemperature("temperatureEnable", "Enable temerature display: ", WMtempEnable, 2);

void setup() {

    // This code is for the first time use only. If there is no initial password and SSID in EEPROM memory, code will crash.
    // EEPROM.begin(512);
    // EEPROM.put(250, SSID);
    // EEPROM.put(280, password);
    // EEPROM.put(310, timeFormat);
    // EEPROM.put(334, timeEnabled);
    // EEPROM.put(342, dateEnabled);
    // EEPROM.put(350, cryptoEnabled);
    // EEPROM.put(358, temperatureEnabled);
    // EEPROM.put(366, setTimeManuallyFlag);
    // EEPROM.put(374, setTimeSemiAutoFlag);
    // EEPROM.put(382, setTimeAutoFlag);
    // EEPROM.put(398, timeZoneOffset);
    // EEPROM.put(414, dst);
    // EEPROM.commit();

    // Touch button interrupt.
    attachInterrupt(digitalPinToInterrupt(TOUCH_BUTTON), tuchButtonPressed, RISING);

    #ifdef DEBUG
        delay(6000);    // To have time to open a serial monitor.
    #endif // DEBUG
    
    // WiFiManager. For configuring WiFi access point, setting up the NixieTap parameters and so on...
    // Adding parameters to Settings window in WiFiManager AP.
    wifiManager.addParameter(&text0);
    wifiManager.addParameter(&setTimeManually);
    wifiManager.addParameter(&setTimeSemiAuto);
    wifiManager.addParameter(&setTimeAuto);
    wifiManager.addParameter(&text1);
    wifiManager.addParameter(&text2);
    wifiManager.addParameter(&yearWM);
    wifiManager.addParameter(&monthWM);
    wifiManager.addParameter(&dayWM);
    wifiManager.addParameter(&hoursWM);
    wifiManager.addParameter(&minutesWM);
    wifiManager.addParameter(&formatWM);
    wifiManager.addParameter(&text21);
    wifiManager.addParameter(&text22);
    wifiManager.addParameter(&timeZoneOffsetWM);
    wifiManager.addParameter(&dstWM);
    wifiManager.addParameter(&text3);
    wifiManager.addParameter(&text4);
    wifiManager.addParameter(&timezonedbKey);
    wifiManager.addParameter(&ipStackKey);
    wifiManager.addParameter(&googleLocKey);
    wifiManager.addParameter(&googleTimeZoneKey);
    wifiManager.addParameter(&openWeatherMapKey);
    wifiManager.addParameter(&openWeatherMapFormat);
    wifiManager.addParameter(&text5);
    wifiManager.addParameter(&text6);
    wifiManager.addParameter(&cryptoID);
    wifiManager.addParameter(&text7);
    wifiManager.addParameter(&text8);
    wifiManager.addParameter(&text9);
    wifiManager.addParameter(&ssid);
    wifiManager.addParameter(&pass);
    wifiManager.addParameter(&text10);
    wifiManager.addParameter(&enableTime);
    wifiManager.addParameter(&enableDate);
    wifiManager.addParameter(&enableCrypto);
    wifiManager.addParameter(&enableTemperature);
    // Determining the look of the WiFiManager Web Server, which buttons will be visible on a main tab.
    std::vector<const char *> menu = {"wifi","param","info","sep","erase","exit"};
    wifiManager.setMenu(menu);

    readParameters();           // Reed all stored parameters from EEPROM.
    setSyncProvider(RTC.get);   // the function to get the time from the RTC
    enableSecDot();
    #ifdef DEBUG
        if(timeStatus()!= timeSet)
            Serial.println("Unable to sync with the RTC!");
        else
            Serial.println("RTC has set the system time!");
    #endif // DEBUG
}
void loop() {
    configButton = digitalRead(CONFIG_BUTTON);
    if(configButton) {
        startPortalManually(); // This function allows you to manually start the access point on demand. (By pressing the button on the back of the device.)
    }
    // If the time is configured to be set semi-auto or auto and NixiTap is just started, the NTP time request is created.
    if((setTimeSemiAutoFlag || setTimeAutoFlag) && wifiFirstConnected && WiFi.status() == WL_CONNECTED) {
        NTP.onNTPSyncEvent([](NTPSyncEvent_t event) {ntpEvent = event; syncEventTriggered = true;});
        NTP.begin();
        wifiFirstConnected = false;
    }
    if(syncEventTriggered) {
        // When the syncEventTriggered is triggered, through the NTPClient, Nixie checks if the NTP time is received.
        // If the NTP time is received, Nixie starts synchronization of RTC time with received NTP time and stops NTPClinet from sending new requests.
        // If the time is configured semi-auto, Nixie is using saved parameters for time zone offset and dst to properly configure the time before updating it on RTC.
        // If the time is set automatically, Nixie sends request to the Time Zone API after which it configures time according to new tz and dst values and saves them in EEPROM memory.
        processSyncEvent(ntpEvent);
        syncEventTriggered = false;
    }
    
    // When the tuch button is pressed nixie tubes will change the displaying mode. 
    if(timeEnabled || dateEnabled || cryptoEnabled || temperatureEnabled) {
        if(state > 3) state = 0;
        if(state == 0 && timeEnabled) {
            // Display time.
            currentMinutes = minute();
            if(currentMinutes != prevMinutes) { // Update the display only if time has changed.
                prevMinutes = currentMinutes;
                t = now();
            }
            if(dot_state != prev_dot_state) {
                prev_dot_state = dot_state;
                nixieTap.writeTime(t, dot_state, timeFormat);
            }
        } else if(!timeEnabled && state == 0) state++;

        if(state == 1 && dateEnabled) {
            // Display date.
            t = now();
            nixieTap.writeDate(t, 1);
        } else if(!dateEnabled && state == 1) state++;
        
        if(state == 2 && cryptoEnabled) {
            // Cryptocurrency price
            if(currencyID >= 1 && currencyID <= 9999) {    // If the currency is not selected, this step will be skipped.
                if(cryptoRefreshFlag) {
                    cryptoRefreshFlag = 0;
                    cryptoCurrencyPrice = nixieTapAPI.getCryptoPrice(currencyID);
                }
                nixieTap.writeNumber(cryptoCurrencyPrice, 350);
            } else state++;
        } else if(!cryptoEnabled && state == 2) state++;

        if(state == 3 && temperatureEnabled) {
            // Display temperature.
            if(weatherKey[0] != '\0') {
                if(weatherRefreshFlag) {
                    weatherRefreshFlag = 0;
                    if(!locationObtained) {
                        loc = nixieTapAPI.getLocation();
                    }
                    if(loc != "0") {
                        locationObtained = true;
                        temperature = nixieTapAPI.getTempAtMyLocation(loc, weatherFormat);
                    } else state++;
                }
                nixieTap.writeNumber(temperature, 0);
            } else state++;
        } else if(!temperatureEnabled && state == 3) state++;
    }

    // Here you can add new functions for displaying numbers on NixieTap, just follow the basic writing principle from above.
}

void startPortalManually() {
    // By pressing the button on the back of the device you can manually start the WiFi Manager and access it's settings.
    nixieTap.write(10, 10, 10, 10, 0);
    disableSecDot(); // If the dots are not disabled, precisely the RTC_IRQ_PIN interrupt, ConfigPortal will chrach.
    movingDot.attach(0.2, scrollDots);
    #ifdef DEBUG
        Serial.println("---------------------------------------------------------------------------------------------");
    #endif // DEBUG
    wifiManager.setConfigPortalTimeout(1800);
    // This will run a new config portal if the conditions are met.
    if(!wifiManager.startConfigPortal(SSID, password)) {
        #ifdef DEBUG
            Serial.println("Failed to connect and hit timeout!");
        #endif // DEBUG
        // If the NixieTap is not connected to WiFi, it will collect the entered parameters and configure the RTC according to them.
    }
    updateParameters();
    updateTime();
    movingDot.detach();
    nixieTap.write(10, 10, 10, 10, 0); // Deletes remaining dot on display.
    enableSecDot();
}

void processSyncEvent(NTPSyncEvent_t ntpEvent) {
    #ifdef DEBUG
        Serial.println("---------------------------------------------------------------------------------------------");
    #endif
        if(ntpEvent < 0) {
            #ifdef DEBUG
                Serial.print("Time Sync error: ");
                if(ntpEvent == noResponse) {
                    Serial.println("NTP server not reachable.");
                } else if(ntpEvent == invalidAddress) {
                    Serial.println("Invalid NTP server address.");
                } else if (ntpEvent == errorSending) {
                    Serial.println ("Error sending request");
                } else if (ntpEvent == responseError) {
                    Serial.println ("NTP response error");
                }
                Serial.println("Synchronization will be attempted again after 15 seconds.");
                Serial.println("If the time is not synced after 2 minutes, please restart Nixie Tap and try again!");
                Serial.println("If restart does not help. There might be a problem with the NTP server or your WiFi connection. You can set the time manually.");
            #endif
        } else {
            if(NTP.getLastNTPSync() != 0) {
                #ifdef DEBUG
                    Serial.print("NTP time is obtained: ");
                    Serial.println(NTP.getLastNTPSync());
                #endif
                if(setTimeAutoFlag) {
                    #ifdef DEBUG
                        Serial.println("Auto time adjustment started!");
                    #endif // DEBUG
                    int16_t newTimeZoneOffset;
                    uint8_t newdst;
                    newTimeZoneOffset = nixieTapAPI.getTimezoneOffset(NTP.getLastNTPSync(), &newdst);
                    // New timeZoneOffset and dst must be saved this way because they do not come from WIFIManager API.
                    if(newTimeZoneOffset != timeZoneOffset || newdst != dst) {
                        EEPROM.begin(512);
                        if(newTimeZoneOffset != timeZoneOffset) {
                            timeZoneOffset = newTimeZoneOffset;
                            EEPROM.put(398, timeZoneOffset);
                        }
                        if(newdst != dst) {
                            dst = newdst;
                            EEPROM.put(414, dst);
                        }
                        EEPROM.commit();
                    }
                }
                #ifdef DEBUG
                    if(setTimeSemiAutoFlag) {
                        Serial.println("Semi-auto time adjustment started!");
                    }
                #endif // DEBUG
                // Collect NTP time, put it in RTC and stop NTP synchronization.
                RTC.set(NTP.getLastNTPSync() + timeZoneOffset*60 + dst*60*60);
                NTP.stop();
                setSyncProvider(RTC.get);
                wifiFirstConnected = false;
            }
        }
}
void readParameters() {
    #ifdef DEBUG
        Serial.println("---------------------------------------------------------------------------------------------");
        Serial.println("Reading saved parameters from EEPROM.");
    #endif
    EEPROM.begin(512);
    int EEaddress = 0;
    EEPROM.get(EEaddress, tzdbKey);
    if(tzdbKey[0] != '\0')
        nixieTapAPI.applyKey(tzdbKey, 0);
    EEaddress += 50;
    EEPROM.get(EEaddress, stackKey);
    if(stackKey[0] != '\0')
        nixieTapAPI.applyKey(stackKey, 1);
    EEaddress += 50;
    EEPROM.get(EEaddress, googleLKey);
    if(googleLKey[0] != '\0')
        nixieTapAPI.applyKey(googleLKey, 2);
    EEaddress += 50;
    EEPROM.get(EEaddress, googleTZkey);
    if(googleTZkey[0] != '\0')
        nixieTapAPI.applyKey(googleTZkey, 3);
    EEaddress += 50;
    EEPROM.get(EEaddress, weatherKey);
    if(weatherKey[0] != '\0')
        nixieTapAPI.applyKey(weatherKey, 4);
    EEaddress += 50;
    EEPROM.get(EEaddress, SSID);
    EEaddress += 30;
    EEPROM.get(EEaddress, password);
    EEaddress += 30;
    EEPROM.get(EEaddress, timeFormat);
    EEaddress += sizeof(uint8_t);
    EEPROM.get(EEaddress, currencyID);
    EEaddress += sizeof(uint16_t);
    EEPROM.get(EEaddress, weatherFormat);
    EEaddress += sizeof(uint8_t);
    EEPROM.get(EEaddress, timeEnabled);
    EEaddress += sizeof(uint8_t);
    EEPROM.get(EEaddress, dateEnabled);
    EEaddress += sizeof(uint8_t);
    EEPROM.get(EEaddress, cryptoEnabled);
    EEaddress += sizeof(uint8_t);
    EEPROM.get(EEaddress, temperatureEnabled);
    EEaddress += sizeof(uint8_t);
    EEPROM.get(EEaddress, setTimeManuallyFlag);
    EEaddress += sizeof(uint8_t);
    EEPROM.get(EEaddress, setTimeSemiAutoFlag);
    EEaddress += sizeof(uint8_t);
    EEPROM.get(EEaddress, setTimeAutoFlag);
    EEaddress += sizeof(uint8_t);
    EEPROM.get(EEaddress, timeZoneOffset);
    EEaddress += sizeof(int16_t);
    EEPROM.get(EEaddress, dst);

    #ifdef DEBUG
        Serial.println("---------------------------------------------------------------------------------------------");
        Serial.println("Saved API Keys in EEPROM memory: ");
        if(tzdbKey[0] != '\0')
            Serial.println("  Timezonedb Key: " + String(tzdbKey));
        if(stackKey[0] != '\0')
            Serial.println("  Ipstack Key: " + String(stackKey));
        if(googleLKey[0] != '\0')
            Serial.println("  Google Location Key: " + String(googleLKey));
        if(googleTZkey[0] != '\0')
            Serial.println("  Google Time Zone Key: " + String(googleTZkey));
        if(weatherKey[0] != '\0')
            Serial.println("  OneWeaterMap Key: " + String(weatherKey));
        if(currencyID >=1 && currencyID <=9999)
            Serial.println("  Cryptocurrency ID: " + String(currencyID));
        Serial.printf("  Weather format is (Celsius=1/Fahrenheit=0): %d\n", weatherFormat);
        Serial.printf("  Time format is (24h=1/12h=0): %d\n", timeFormat);
        Serial.println("  SSID: " + String(SSID));
        Serial.println("  Password: " + String(password));
        Serial.println("  Time displaing enable: " + String(timeEnabled) + ", Date displaing enable: " + String(dateEnabled));
        Serial.println("  Crypto displaing enable: " + String(cryptoEnabled) + ", Temperature displaing enable: " + String(temperatureEnabled));
        Serial.println("setTimeManuallyFlag: " + String(setTimeManuallyFlag));
        Serial.println("setTimeSemiAutoFlag: " + String(setTimeSemiAutoFlag));
        Serial.println("setTimeAutoFlag: " + String(setTimeAutoFlag));
        Serial.println("timeZoneOffset: " + String(timeZoneOffset));
        Serial.println("dst: " + String(dst));
        Serial.println("---------------------------------------------------------------------------------------------");
    #endif // DEBUG
}
void updateParameters() {
    #ifdef DEBUG
        Serial.println("---------------------------------------------------------------------------------------------");
        Serial.println("Synchronization of parameters started.");
    #endif // DEBUG
    EEPROM.begin(512); // Number of bytes to allocate for parameters.
    int EEaddress = 0;
    char temp[50];
    #ifdef DEBUG
        Serial.println("Comparing entered keys with the saved ones.");
    #endif // DEBUG
    strcpy(temp, timezonedbKey.getValue());
    if(strcmp(temp, tzdbKey) && temp[0] != '\0') {   // If the keys are different, old key will be replaced with the new one.
        strcpy(tzdbKey, temp);
        nixieTapAPI.applyKey(tzdbKey, 0);
        EEPROM.put(EEaddress, tzdbKey);
        #ifdef DEBUG
            Serial.println("Timezonedb key updated!");
        #endif // DEBUG
    }
    EEaddress += 50;
    strcpy(temp, ipStackKey.getValue());
    if(strcmp(temp, stackKey) && temp[0] != '\0') {
        strcpy(stackKey, temp);
        nixieTapAPI.applyKey(stackKey, 1);
        EEPROM.put(EEaddress, stackKey);
        #ifdef DEBUG
            Serial.println("Stack key updated!");
        #endif // DEBUG
    }
    EEaddress += 50;
    strcpy(temp, googleLocKey.getValue());
    if(strcmp(temp, googleLKey) && temp[0] != '\0') {
        strcpy(googleLKey, temp);
        nixieTapAPI.applyKey(googleLKey, 2);
        EEPROM.put(EEaddress, googleLKey);
        #ifdef DEBUG
            Serial.println("Google Location key updated!");
        #endif // DEBUG
    }
    EEaddress += 50;
    strcpy(temp, googleTimeZoneKey.getValue());
    if(strcmp(temp, googleTZkey) && temp[0] != '\0') {
        strcpy(googleTZkey, temp);
        nixieTapAPI.applyKey(googleTZkey, 3);
        EEPROM.put(EEaddress, googleTZkey);
        #ifdef DEBUG
            Serial.println("Google Time Zone key updated!");
        #endif // DEBUG
    }
    EEaddress += 50;
    strcpy(temp, openWeatherMapKey.getValue());
    if(strcmp(temp, weatherKey) && temp[0] != '\0') {
        strcpy(weatherKey, temp);
        nixieTapAPI.applyKey(weatherKey, 4);
        EEPROM.put(EEaddress, weatherKey);
        #ifdef DEBUG
            Serial.println("OneWeatherMap key updated!");
        #endif // DEBUG
    }
    EEaddress += 50;
    strcpy(temp, ssid.getValue());
    if(strcmp(temp, SSID) && temp[0] != '\0') {
        strcpy(SSID, temp);
        EEPROM.put(EEaddress, SSID);
        #ifdef DEBUG
            Serial.println("New SSID saved!");
        #endif // DEBUG
    }
    EEaddress += 30;
    strcpy(temp, pass.getValue());
    if(strcmp(temp, password) && temp[0] != '\0') {
        strcpy(password, temp);
        EEPROM.put(EEaddress, password);
        #ifdef DEBUG
            Serial.println("New password saved!");
        #endif // DEBUG
    }
    EEaddress += 30;
    strcpy(WMTimeFormat, formatWM.getValue());
    uint8_t newTimeFormat = atoi(WMTimeFormat);
    if(WMTimeFormat[0] != '\0' && timeFormat != newTimeFormat && (newTimeFormat == 1 || newTimeFormat == 0)) {
        timeFormat = newTimeFormat;
        EEPROM.put(EEaddress, timeFormat);
        #ifdef DEBUG
            Serial.println("Time format updated!");
        #endif // DEBUG
    }
    EEaddress += sizeof(uint8_t);
    strcpy(WMcurrencyID, cryptoID.getValue());
    uint16_t newCurrencyID = atoi(WMcurrencyID);
    if(WMcurrencyID[0] != '\0' && currencyID != newCurrencyID && newCurrencyID >= 1 && newCurrencyID <= 9999) {
        cryptoRefreshFlag = 1;
        currencyID = newCurrencyID;
        EEPROM.put(EEaddress, currencyID);
        #ifdef DEBUG
            Serial.println("Currency ID updated!");
        #endif // DEBUG
    }
    EEaddress += sizeof(uint16_t);
    strcpy(WMweatherFormat, openWeatherMapFormat.getValue());
    uint8_t newWeatherFormat = atoi(WMweatherFormat);
    if(WMweatherFormat[0] != '\0' && weatherFormat != newWeatherFormat && (newWeatherFormat == 1 || newWeatherFormat == 0)) {
        weatherRefreshFlag = 1;
        weatherFormat = newWeatherFormat;
        EEPROM.put(EEaddress, weatherFormat);
        #ifdef DEBUG
            Serial.println("Weather format updated!");
        #endif // DEBUG
    }
    EEaddress += sizeof(uint8_t);
    strcpy(WMtimeEnable, enableTime.getValue());
    uint8_t newTimeEnable = atoi(WMtimeEnable);
    if(WMtimeEnable[0] != '\0' && timeEnabled != newTimeEnable && (newTimeEnable == 1 || newTimeEnable == 0)) {
        timeEnabled = newTimeEnable;
        EEPROM.put(EEaddress, timeEnabled);
        #ifdef DEBUG
            Serial.println("Time enable status changed.");
        #endif // DEBUG
    }
    EEaddress += sizeof(uint8_t);
    strcpy(WMdateEnable, enableDate.getValue());
    uint8_t newDateEnable = atoi(WMdateEnable);
    if(WMdateEnable[0] != '\0' && dateEnabled != newDateEnable && (newDateEnable == 1 || newDateEnable == 0)) {
        dateEnabled = newDateEnable;
        EEPROM.put(EEaddress, dateEnabled);
        #ifdef DEBUG
            Serial.println("Date enable status changed.");
        #endif // DEBUG
    }
    EEaddress += sizeof(uint8_t);
    strcpy(WMcryptoEnable, enableCrypto.getValue());
    uint8_t newCryptoEnable = atoi(WMcryptoEnable);
    if(WMcryptoEnable[0] != '\0' && cryptoEnabled != newCryptoEnable && (newCryptoEnable == 1 || newCryptoEnable == 0)) {
        cryptoEnabled = newCryptoEnable;
        EEPROM.put(EEaddress, cryptoEnabled);
        #ifdef DEBUG
            Serial.println("Crypto enable status changed.");
        #endif // DEBUG
    }
    EEaddress += sizeof(uint8_t);
    strcpy(WMtempEnable, enableTemperature.getValue());
    uint8_t newTempEnable = atoi(WMtempEnable);
    if(WMtempEnable[0] != '\0' && temperatureEnabled != newTempEnable && (newTempEnable == 1 || newTempEnable == 0)) {
        temperatureEnabled = newTempEnable;
        EEPROM.put(EEaddress, temperatureEnabled);
        #ifdef DEBUG
            Serial.println("Temperature enable status changed.");
        #endif // DEBUG
    }
    EEaddress += sizeof(uint8_t);
    strcpy(WMsetTimeManuallyFlag, setTimeManually.getValue());
    uint8_t newSetTimeManuallyFlag = atoi(WMsetTimeManuallyFlag);
    if(WMsetTimeManuallyFlag[0] != '\0' && setTimeManuallyFlag != newSetTimeManuallyFlag && (newSetTimeManuallyFlag == 1 || newSetTimeManuallyFlag == 0)) {
        setTimeManuallyFlag = newSetTimeManuallyFlag;
        EEPROM.put(EEaddress, setTimeManuallyFlag);
        #ifdef DEBUG
            Serial.println("setTimeManualyFlag status changed to: " + String(WMsetTimeManuallyFlag));
        #endif // DEBUG
    }
    EEaddress += sizeof(uint8_t);
    strcpy(WMsetTimeSemiAutoFlag, setTimeSemiAuto.getValue());
    uint8_t newSetTimeSemiAutoFlag = atoi(WMsetTimeSemiAutoFlag);
    if(WMsetTimeSemiAutoFlag[0] != '\0' && setTimeSemiAutoFlag != newSetTimeSemiAutoFlag && (newSetTimeSemiAutoFlag == 1 || newSetTimeSemiAutoFlag == 0)) {
        setTimeSemiAutoFlag = newSetTimeSemiAutoFlag;
        EEPROM.put(EEaddress, setTimeSemiAutoFlag);
        #ifdef DEBUG
            Serial.println("setSemiAutoFlag status changed to: " + String(WMsetTimeSemiAutoFlag));
        #endif // DEBUG
    }
    EEaddress += sizeof(uint8_t);
    strcpy(WMsetTimeAutoFlag, setTimeAuto.getValue());
    uint8_t newSetTimeAutoFlag = atoi(WMsetTimeAutoFlag);
    if(WMsetTimeAutoFlag[0] != '\0' && setTimeAutoFlag != newSetTimeAutoFlag && (newSetTimeAutoFlag == 1 || newSetTimeAutoFlag == 0)) {
        setTimeAutoFlag = newSetTimeAutoFlag;
        EEPROM.put(EEaddress, setTimeAutoFlag);
        #ifdef DEBUG
            Serial.println("setTimeAutoFlag status changed to: " + String(WMsetTimeAutoFlag));
        #endif // DEBUG
    }
    EEaddress += sizeof(uint8_t);
    strcpy(WMTimeZoneOffset, timeZoneOffsetWM.getValue());
    int16_t newTimeZoneOffset = atoi(WMTimeZoneOffset);
    if(WMTimeZoneOffset[0] != '\0' && timeZoneOffset != newTimeZoneOffset && (newTimeZoneOffset/60) <= 14 && (newTimeZoneOffset/60) >= -12) {
        timeZoneOffset = newTimeZoneOffset;
        EEPROM.put(EEaddress, timeZoneOffset);
        #ifdef DEBUG
            Serial.println("timeZoneOffset changed to: " + String(timeZoneOffset));
        #endif // DEBUG
    }
    EEaddress += sizeof(int16_t);
    strcpy(WMdst, dstWM.getValue());
    uint8_t newdst = atoi(WMdst);
    if(WMdst[0] != '\0' && dst != newdst && (newdst == 1 || newdst == 0)) {
        dst = newdst;
        EEPROM.put(EEaddress, dst);
        #ifdef DEBUG
            Serial.println("dst status changed to: " + String(dst));
        #endif // DEBUG
    }
    EEPROM.commit();
    
    if(cryptoEnabled) {
        priceRefresh.attach(300, cryptoRefresh); // This will refresh the cryptocurrency price every 5min.
    } else 
        priceRefresh.detach();
    if(temperatureEnabled) {
        temperatureRefresh.attach(3600, weatherRefresh);
    } else
        temperatureRefresh.detach();
    #ifdef DEBUG
        Serial.println("Synchronization of parameters completed!");
    #endif // DEBUG
}

void updateTime() {
    if(setTimeManuallyFlag) { // I need feedback from the WiFiManager API that this option has been selected.
        NTP.stop();     // NTP sync is disableded to avoid sync errors.
        uint8_t newTime = 0;
        // Store the manually set time and date in the local memory.
        strcpy(WMYear, yearWM.getValue());
        strcpy(WMMonth, monthWM.getValue());
        strcpy(WMDay, dayWM.getValue());
        strcpy(WMHours, hoursWM.getValue());
        strcpy(WMMinutes, minutesWM.getValue());
        // Convert parameters from char to int.
        if(yearInt != atoi(WMYear) && WMYear[0] != '\0') {
            yearInt = atoi(WMYear);
            newTime = 1;
        }
        if(monthInt != atoi(WMMonth) && WMMonth[0] != '\0') {
            monthInt = atoi(WMMonth);
            newTime = 1;
        }
        if(dayInt != atoi(WMDay) && WMDay[0] != '\0') {
            dayInt = atoi(WMDay);
            newTime = 1;
        }
        if(hoursInt != atoi(WMHours) && WMHours[0] != '\0') {
            hoursInt = atoi(WMHours);
            newTime = 1;
        }
        if(minutesInt != atoi(WMMinutes) && WMMinutes[0] != '\0') {
            minutesInt = atoi(WMMinutes);
            newTime = 1;
        }
        // Check if the parameters are changed. If so, then enter in manual date and time configuration mode.
        if(newTime) {
            // Basic check for entered data. It is not thorough, there is still room for improvement.
            if(nixieTap.checkDate(yearInt, monthInt, dayInt, hoursInt, minutesInt)) {
                setTime(hoursInt, minutesInt, 0, dayInt, monthInt, yearInt);
                t = now();
                RTC.set(t);
                setSyncProvider(RTC.get);
                #ifdef DEBUG
                    Serial.println("Manually entered date and time saved!");
                #endif // DEBUG
            } else {
                #ifdef DEBUG
                    Serial.println("Incorect date and time parameters, please try again!");
                #endif // DEBUG
            }
        }
    } else if((setTimeSemiAutoFlag || setTimeAutoFlag) && (WiFi.status() == WL_CONNECTED)) {
        NTP.onNTPSyncEvent([](NTPSyncEvent_t event) {ntpEvent = event; syncEventTriggered = true;});
        NTP.begin();
        wifiFirstConnected = false;
    }
}

/*                                                           *
 *  Enables the center dot to change its state every second. *
 *                                                           */
void enableSecDot() {
    if(secDotDef == false) {
        detachInterrupt(RTC_IRQ_PIN);
        RTC.setIRQ(1);  // Configures the 512Hz interrupt from RTC.
        attachInterrupt(digitalPinToInterrupt(RTC_IRQ_PIN), irq_1Hz_int, FALLING);
        secDotDef = true;
        stopDef = false;
    }
}
/*                                                *
 * Disaling the dots function on nixie display.   *
 *                                                */
void disableSecDot() {
    if(stopDef == false) {
        detachInterrupt(RTC_IRQ_PIN);
        RTC.setIRQ(0);              // Configures the interrupt from RTC.
        dotPosition = 0b10;         // Restast dot position.
        stopDef = true;
        secDotDef = false;
    }
}
/*                                                                                       *
 * An interrupt function that changes the state and position of the dots on the display. *
 *                                                                                       */
void scrollDots() {
    if(dotPosition == 0b100000) dotPosition = 0b10;
    nixieTap.write(11, 11, 11, 11, dotPosition);
    dotPosition = dotPosition << 1;
}
/*                                                                  *
 * An interrupt function for changing the dot state every 1 second. *
 *                                                                  */
void irq_1Hz_int() {
    dot_state = !dot_state;
}
/*                                                                *
 * An interrupt function for the touch sensor when it is touched. *
 *                                                                */
void tuchButtonPressed() {
    state++;
}
void cryptoRefresh() {
    cryptoRefreshFlag = 1;
}
void weatherRefresh() {
    weatherRefreshFlag = 1;
}