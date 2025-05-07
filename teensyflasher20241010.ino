//I think this one is working, page offset is the key!
#include <USBHost_t36.h>
#include <SPI.h>

// USB Host and Drive
USBHost myusb;
USBDrive usbDrive(myusb);
USBFilesystem firstPartition(myusb);
File hexFile;                // File object for reading the hex file

//8mhz / 6 according to the datasheet
#define spiclock 8000000/6
uint16_t extendedAddress;
// SPI Pins
#define MOSI_PIN 26
#define MISO_PIN 39
#define SCK_PIN 27
#define RESET_PIN 38

bool flashSuccess;
// LED Pin for Status Indication
#define STATUS_LED_PIN 13

#define blink digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));

#define BUFFERSIZE 1024*486
DMAMEM char buffer[BUFFERSIZE];

void setup() {

    flashSuccess=false;
    Serial.begin(115200);
    pinMode(STATUS_LED_PIN, OUTPUT);
    myusb.begin();
    if (usbDrive.begin()) {
        Serial.println("USB Drive detected!");

        // Mount filesystem and open file
        while (!firstPartition) {
          myusb.Task();
        }
        if (firstPartition) {
            // Open root directory and print the contents
            File root = firstPartition.open("/");
            printDirectory(root, 0);
            hexFile = firstPartition.open("firmware.hex");
            if (hexFile) {
                  // Set up RESET pin
                pinMode(RESET_PIN, OUTPUT);
                digitalWrite(RESET_PIN, HIGH);  // Keep the ATmega in a normal state initially

                delay(5);
                  // Set up SPI1 pins and initialize SPI1
                SPI1.setMOSI(MOSI_PIN);
                SPI1.setMISO(MISO_PIN);
                SPI1.setSCK(SCK_PIN);
                SPI1.begin();
                Serial.println("HEX file opened successfully, starting programming...");
                    // Enter programming mode and check device communication
                if (enterProgrammingMode())
                {
                  readDeviceID();
                  eraseFlash();
                  programATmega2560(hexFile);  // Start programming
                  Serial.println("Programming finished.");
                }
                SPI1.end();
                //release reset after programming
                delay(100);
                digitalWrite(RESET_PIN, HIGH);
                flashSuccess=true;
            } else {
                Serial.println("Failed to open firmware.hex file.");
            }

        } else {
            Serial.println("Failed to mount filesystem.");
        }
    } else {
        Serial.println("No USB drive detected.");
    }

    pinMode(RESET_PIN, INPUT_PULLUP);
    pinMode(SCK_PIN, INPUT_PULLUP);
    pinMode(MOSI_PIN, INPUT_PULLUP);
    pinMode(MISO_PIN, INPUT_PULLUP);
}

void loop() {
    blink;
    
    if (flashSuccess)
     delay(1500);
    else
      delay(100);
}

bool enterProgrammingMode() {
  bool success = false;
    // Bring RESET low to enter programming mode and keep it low
    digitalWrite(RESET_PIN, LOW);
    delay(100);  // Hold RESET low for a bit longer
    
    // Lower the SPI speed further to accommodate slower clock speeds on the target
    SPI1.beginTransaction(SPISettings(spiclock, MSBFIRST, SPI_MODE0));

    // Send the Programming Enable Command
    Serial.println("Sending Programming Enable Command: 0xAC 0x53 0x00 0x00");

    uint8_t response;

    for (int i = 0; i < 3; i++) {  // Attempt to enter programming mode up to 3 times
        SPI1.transfer(0xAC);  // Programming Enable Command
        SPI1.transfer(0x53);  // Expect echo of 0x53

        response = SPI1.transfer(0x00);  // Padding byte
        SPI1.transfer(0x00);  // Padding byte
        // Print both responses for better visibility

        Serial.print("Response attempt ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.println(response, HEX);

        if (response == 0x53) {  // Check for the correct echo (0x53)
            Serial.println("Successfully entered programming mode.");
            success = true;
            break;
        } else {
            Serial.println("Failed to enter programming mode. Pulsing RESET and retrying...");
            digitalWrite(RESET_PIN, HIGH);  // Pulse RESET high and retry
            delay(50);  // Delay before retry
            digitalWrite(RESET_PIN, LOW);  // Keep RESET low for next attempt
        }

        delay(25);  // Small delay before retrying
    }

    SPI1.endTransaction();

    if (!success) {
        Serial.println("Failed to enter programming mode after multiple attempts.");
    }
    return success;
}


void readDeviceID() {
    SPI1.beginTransaction(SPISettings(spiclock, MSBFIRST, SPI_MODE0));

    // Read Manufacturer ID
    Serial.println("Reading Manufacturer ID...");
    SPI1.transfer(0x30);  // Read Device ID command
    SPI1.transfer(0x00);  // Padding byte
    SPI1.transfer(0x00);  // Address byte for Manufacturer ID
    uint8_t manufacturerID = SPI1.transfer(0x00);  // Manufacturer ID

    // Read Family ID
    Serial.println("Reading Family ID...");
    SPI1.transfer(0x30);  // Read Device ID command
    SPI1.transfer(0x00);  // Padding byte
    SPI1.transfer(0x01);  // Address byte for Family ID
    uint8_t familyID = SPI1.transfer(0x00);  // Family ID

    // Read Flash Size
    Serial.println("Reading Flash Size...");
    SPI1.transfer(0x30);  // Read Device ID command
    SPI1.transfer(0x00);  // Padding byte
    SPI1.transfer(0x02);  // Address byte for Flash Size
    uint8_t flashSize = SPI1.transfer(0x00);  // Flash Size

    SPI1.endTransaction();

    // Print results
    Serial.print("Manufacturer ID: ");
    Serial.println(manufacturerID, HEX);
    Serial.print("Family ID: ");
    Serial.println(familyID, HEX);
    Serial.print("Flash Size: ");
    Serial.println(flashSize, HEX);
}


// Function to erase the flash memory
void eraseFlash() {
    SPI1.beginTransaction(SPISettings(spiclock, MSBFIRST, SPI_MODE0));
    SPI1.transfer(0xAC);  // Chip erase command
    SPI1.transfer(0x80);
    SPI1.transfer(0x00);
    SPI1.transfer(0x00);
    SPI1.endTransaction();

    delay(50);  // Wait for the erase to complete
    Serial.println("Flash memory erased.");
}

// Function to program the ATmega2560
void programATmega2560(File &hexFile) {
    uint8_t data[] = { 0x00, 0x00 };
    setExtendedAddress(data);
    Serial.print("Making buffer array....");

    char lineBuffer[256];
    Serial.println("done.");
    uint32_t characters = 0;
    
    while (hexFile.available()) {
        size_t len = hexFile.readBytesUntil('\n', lineBuffer, sizeof(buffer));
        if (len > 0) {

            for (int i = 0; i < len+1; i++)
            {
              buffer[characters]=lineBuffer[i];
              characters=characters+1;
            }
            Serial.print(".");
            blink;
        }
        if (characters > BUFFERSIZE) {
          Serial.println("");
          Serial.println("program is too big!");
          hexFile.close();
          return;
        }
    }
    hexFile.close();
    Serial.println("");
    Serial.print("Done reading firmware.hex into ram. Program ");
    Serial.println(characters);
    Serial.println(" characters long.");

    uint8_t lineChar = 0;
    for (uint32_t i = 0; i < characters; i++)
    {
        lineBuffer[lineChar] = buffer[i];
        lineChar=lineChar+1;
        if ((buffer[i+1]==':') || (i == characters))
        {
            lineBuffer[lineChar] = '\0';  // Null-terminate the line
            blink;
            if(!sendHexLine(lineBuffer)){
                Serial.println("Programming failed.");
                return;
            }
            lineChar=0;
        }

    }
    Serial.println("Programming complete.");
}

// Function to parse and send a hex line
bool sendHexLine(const char *line) {
    if (line[0] != ':') {
        Serial.println("Invalid hex line.");
        return false;
    }

    uint8_t byteCount = (hexToByte(line + 1) & 0xFF);
    //intel address format (byte address, not word)
    uint16_t address = (hexToByte(line + 3) << 8) | hexToByte(line + 5);
    uint8_t recordType = hexToByte(line + 7);
    uint8_t data[256];
    uint8_t over;
    bool success = true;

    for (uint8_t i = 0; i < byteCount; i++) {
        data[i] = hexToByte(line + 9 + (i * 2));
    }
    if (recordType == 0x00) {
        // Write data to the flash
        Serial.print("Page:");
        Serial.print(getAddrPage(address));
        Serial.print(" Word:");
        Serial.println(getAddrLow(address));
        if(getAddrLow(address)+(byteCount/2) > 128) {
            Serial.print("hex line page end");
            Serial.print(getAddrLow(address)+(byteCount/2));
            Serial.print("crosses page boundary by ");
            over=getAddrLow(address)+(byteCount/2)-128; //in words
            Serial.print(over);
            Serial.println("words");
            success = writeFlashPage(address, data, byteCount-over*2);
            for (uint8_t i = 0; i < over*2; i++) {
                data[i] = data[byteCount-(over*2)+i];
            }
            if (! writeFlashPage(address+(byteCount-(over*2)), data, over*2)) success = false;
        }else{
            success = writeFlashPage(address, data, byteCount);
        }
    } else if (recordType == 0x02){
        // Set extended address
        setExtendedAddress(data);
    }
    return success;
}


bool writeFlashPage(uint16_t address, uint8_t *data, uint8_t length) {
    Serial.print(">");
    SPI1.beginTransaction(SPISettings(spiclock, MSBFIRST, SPI_MODE0));

    // Load data into flash page buffer
    for (uint8_t i = 0; i < length; i++) {
        if (i % 2 == 0) {
            // Write low byte to buffer
            SPI1.transfer(0x40);  // Load Program Memory Page, Low Byte
        } else {
            // Write high byte to buffer
            SPI1.transfer(0x48);  // Load Program Memory Page, High Byte
        }
        SPI1.transfer(0);  // High word of address
        //adding to the low byte here shifts by n words
        SPI1.transfer(getAddrLow(address) + (i>>1));  // Low word of address
        SPI1.transfer(data[i]);  // Data byte to be written
        // Print feedback for debugging
        Serial.print(data[i], HEX);
        Serial.print(".");
    }
    SPI1.transfer(0x4C);  // Write Program Memory Page command
    SPI1.transfer(getAddrHighByte(address));  // High byte of address
    SPI1.transfer(getAddrLowByte(address));  // Low byte of address 
    SPI1.transfer(0x00);  // Padding byte
    busyWait();
    Serial.println("~~!");
    // Commit the page buffer to flash memory

    SPI1.endTransaction();

    return verifyFlashPage(address, data, length);
}

bool verifyFlashPage(uint16_t address, uint8_t *data, uint8_t length) {
    Serial.print("<");
    uint8_t flashDataLow = 0xFF;
    uint8_t flashDataHigh = 0xFF;
    bool success = true;
    SPI1.beginTransaction(SPISettings(spiclock, MSBFIRST, SPI_MODE0));

    for (uint8_t i = 0; i < length; i += 2) {  // Handle 16-bit words in flash memory


        // Read low byte
        SPI1.transfer(0x20);  // Read Program Memory, Low Byte command
        SPI1.transfer(getAddrHighByte(address));  // High byte of address
        SPI1.transfer(getAddrLowByte(address+i));  // Low byte of address
        flashDataLow = SPI1.transfer(0x00);  // Read low byte
        
        // Read high byte
        SPI1.transfer(0x28);  // Read Program Memory, High Byte command
        SPI1.transfer(getAddrHighByte(address));  // High byte of address
        SPI1.transfer(getAddrLowByte(address+i));  // Low byte of address
        flashDataHigh = SPI1.transfer(0x00);  // Read high byte


        Serial.print(flashDataLow, HEX);
        // Compare with expected data
        if (flashDataLow != data[i]) {
            success=false;
            Serial.print("X");
        }else{
            Serial.print(".");
        }
        Serial.print(flashDataHigh, HEX);
        if (flashDataHigh != data[i + 1]) {
            success=false;
            Serial.print("X");
        }else{
            Serial.print(".");
        }

    }
    if (success==false)
    {
        Serial.print("BAD!");
    }else{
        Serial.println("OK!");
    }
    SPI1.endTransaction();
    return success;
}


// Helper function to convert hex characters to byte
uint8_t hexToByte(const char *hex) {
    uint8_t highNibble = hex[0] > '9' ? hex[0] - 'A' + 10 : hex[0] - '0';
    uint8_t lowNibble = hex[1] > '9' ? hex[1] - 'A' + 10 : hex[1] - '0';
    return (highNibble << 4) | lowNibble;
}

void printDirectory(File dir, int numSpaces) {
  while (true) {  // More files to print
    File entry = dir.openNextFile();
    if (!entry) {  //No more files or directories - exit loop
      break;
    }
    printSpaces(numSpaces);
    Serial.print(entry.name());
    // If directory, recursively calls itself to get files in that directory
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numSpaces + 2);
    } else {
      // its a file so print size. Directories do not have a size
      printSpaces(48 - numSpaces - strlen(entry.name()));
      Serial.print("  ");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

void printSpaces(int num) {
  for (int i = 0; i < num; i++) {
    Serial.print(" ");
  }
}

void busyWait(){
    uint8_t busy = 0xFF;
    while (busy == 0xFF) {
      //delay(5);
      SPI1.transfer(0xF0);  // Check busy/ready flag
      SPI1.transfer(0x00);  // Padding bytes
      SPI1.transfer(0x00);
      busy = SPI1.transfer(0x00);  // Response
    }
}
// Function to set the extended address byte using SPI
void setExtendedAddress(uint8_t* data) {
    // Extract the extended address from the data bytes (first two bytes of data)
    uint8_t bankByte=0x00;
    Serial.print("Extended address byte: ");
    Serial.println(data[0],HEX);
    if (data[0] == 0x00){
        bankByte=0x00;
        extendedAddress=0;
    }else if(data[0] == 0x10){
        bankByte=0x00;
        extendedAddress=512;
    }else if(data[0] == 0x20){
        bankByte=0x01;
        extendedAddress=0;
    }else if(data[0] == 0x30){
        bankByte=0x01;
        extendedAddress=512;
    }else{
        bankByte=0x00;
        extendedAddress=0;
    }
    // Start the SPI transaction with appropriate settings
    SPI1.beginTransaction(SPISettings(spiclock, MSBFIRST, SPI_MODE0));

    // Send the extended address setup sequence using SPI
    SPI1.transfer(0x4D);  // Command byte to set the extended address
    SPI1.transfer(0x00);  // Extended address setup byte
    SPI1.transfer(bankByte);  // Upper byte of the extended address
    SPI1.transfer(0x00);  // Extended address setup byte

    // End the SPI transaction
    SPI1.endTransaction();

    // Debugging print
    Serial.print("Extended address set to: ");
    Serial.println(extendedAddress);
}
// Returns avr memory page (not including high/low bank) from an intel address
uint16_t getAddrPage(uint16_t address) {
    return (((address >> 7) & 0xFFFF)+extendedAddress);
}

// Returns avr high byte from intel address
uint8_t getAddrHighByte(uint16_t address) {
    //this cuts off the lowest bit of the page number 
    //that is sent in the highest byte of the low address
    return (((address >> 9) & 0xFF)+(extendedAddress>>2)); 
}

// Returns avr memory word number from an intel address
uint8_t getAddrLow(uint16_t address) {
    return (address >> 1) & 0x7F; 
}

// Returns an avr low byte from an intel address, with that high byte low bit
uint8_t getAddrLowByte(uint16_t address) {
    return (address >> 1) & 0xFF; 
}