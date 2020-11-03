/*
 * FirmwareUpdater.cpp
 *
 *  Created on: 21 May 2016
 *      Author: David
 */

#include "FirmwareUpdater.h"

#include "RepRapFirmware.h"
#include "Network.h"
#include "Platform.h"
#include "RepRap.h"
#include "bossa/Samba.h"
#include "bossa/Device.h"
#include "bossa/Flasher.h"
#include "bossa/SerialPort.h"

#if HAS_WIFI_NETWORKING
#include "ESP8266WiFi/WifiFirmwareUploader.h"
#endif

namespace FirmwareUpdater
{
	const unsigned int WifiFirmwareModule = 1;
	// Module 2 used to be the DWC binary file but is no longer used
	const unsigned int WifiExternalFirmwareModule = 3;

	const unsigned int PanelDueFirmwareModule = 4;

	class AuxSerialPort : public SerialPort
	{
	public:
		AuxSerialPort(UARTClass *uartClass) : uart(uartClass), autoflush(false)  {}
	    ~AuxSerialPort() {}

	    bool open(int baud = 115200,
	              int data = 8,
	              SerialPort::Parity parity = SerialPort::ParityNone,
	              SerialPort::StopBit stop = SerialPort::StopBitOne) { return true; }
	    void close() {}

	    bool isUsb() { return false; };

	    int read(uint8_t* data, int size);
	    int write(const uint8_t* data, int size);
	    int get() { return this->uart->read(); }
	    int put(int c) { return this->uart->write(c); }

	    bool timeout(int millisecs) { return true; }
	    void flush() { this->uart->flush(); }
	    void setDTR(bool dtr) {}
	    void setRTS(bool rts) {}
	    void setAutoFlush(bool newAutoflush) { this->autoflush = newAutoflush; }
	private:
		UARTClass *uart;
		bool autoflush;
	};

	int AuxSerialPort::read(uint8_t* data, int size) {
		int res = 0;
    	for (int i = 0; i < size; ++i)
    	{
    		*data = get();
    		++data;
    		++res;
    	}
        return res;
    }

	int AuxSerialPort::write(const uint8_t* data, int size) {
    	auto res = this->uart->write(data, size);
        if (autoflush)
        {
            flush();
        }
        return res;
    }

	// Check that the prerequisites are satisfied.
	// Return true if yes, else print a message and return false.
	bool CheckFirmwareUpdatePrerequisites(uint8_t moduleMap, const StringRef& reply) noexcept
	{
#if HAS_WIFI_NETWORKING
		if ((moduleMap & (1 << WifiExternalFirmwareModule)) != 0 && (moduleMap & (1 << WifiFirmwareModule)) != 0)
		{
			reply.copy("Invalid combination of firmware update modules");
			return false;
		}
		if ((moduleMap & (1 << WifiFirmwareModule)) != 0 && !reprap.GetPlatform().FileExists(DEFAULT_SYS_DIR, WIFI_FIRMWARE_FILE))
		{
			reply.printf("File %s not found", WIFI_FIRMWARE_FILE);
			return false;
		}
#endif
		return true;
	}

	bool IsReady() noexcept
	{
#if HAS_WIFI_NETWORKING
		WifiFirmwareUploader * const uploader = reprap.GetNetwork().GetWifiUploader();
		return uploader == nullptr || uploader->IsReady();
#else
		return true;
#endif
	}

	void UpdateModule(unsigned int module) noexcept
	{
#if HAS_WIFI_NETWORKING
# ifdef DUET_NG
		if (reprap.GetPlatform().IsDuetWiFi())
		{
# endif
			switch(module)
			{
			case WifiExternalFirmwareModule:
				reprap.GetNetwork().ResetWiFiForUpload(true);
				break;

			case WifiFirmwareModule:
				{
					WifiFirmwareUploader * const uploader = reprap.GetNetwork().GetWifiUploader();
					if (uploader != nullptr)
					{
						uploader->SendUpdateFile(WIFI_FIRMWARE_FILE, DEFAULT_SYS_DIR, WifiFirmwareUploader::FirmwareAddress);
					}
				}
				break;
			}
# ifdef DUET_NG
		}
# endif
#endif

		// FIXME: This needs to be rewritten as a separate updater with a state-machine
		if (module == PanelDueFirmwareModule)
		{
			try
			{
				Samba samba;

				AuxSerialPort port(&SERIAL_AUX_DEVICE);
				samba.connect(&port);

				Device device(samba);
				device.create();

				Flash* flash = device.getFlash();

				Flasher flasher(samba, device);

				flasher.lock(false);
				flasher.erase(0);
				flasher.write(PANEL_DUE_FIRMWARE_FILE, DEFAULT_SYS_DIR, 0);

				uint32_t pageErrors;
				uint32_t totalErrors;
				if (!flasher.verify(PANEL_DUE_FIRMWARE_FILE, DEFAULT_SYS_DIR, pageErrors, totalErrors, 0))
				{
					MessageType mt = AddError(MessageType::GenericMessage);
					reprap.GetPlatform().MessageF(mt, "Verify failed: Page errors: %" PRIu32 " - Byte errors: %" PRIu32 "", pageErrors, totalErrors);
					return;
				}

				flash->writeOptions();
				device.reset();
			}
			catch (GCodeException& ex)
			{
				MessageType mt = AddError(MessageType::GenericMessage);
				String<StringLength100> errorMessage;
				ex.GetMessage(errorMessage.GetRef(), nullptr);
				reprap.GetPlatform().MessageF(mt, "%s", errorMessage.c_str());
			}
		}
	}
}

// End
