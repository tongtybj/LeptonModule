#include "Lepton_I2C.h"

#include "leptonSDKEmb32PUB/LEPTON_SDK.h"
#include "leptonSDKEmb32PUB/LEPTON_SYS.h"
#include "leptonSDKEmb32PUB/LEPTON_OEM.h"
#include "leptonSDKEmb32PUB/LEPTON_Types.h"

bool _connected;
LEP_SYS_FPA_TEMPERATURE_KELVIN_T fpa_temp_kelvin;
LEP_SYS_AUX_TEMPERATURE_KELVIN_T aux_temp_kelvin;
LEP_RESULT result;

LEP_CAMERA_PORT_DESC_T _port;

int lepton_connect() {
	LEP_OpenPort(1, LEP_CCI_TWI, 400, &_port);
	_connected = true;
	return 0;
}

void lepton_perform_ffc() {
	if(!_connected) {
		lepton_connect();
	}
	LEP_RunSysFFCNormalization(&_port);
}

//LEP_SYS_FPA_TEMPERATURE_KELVIN_T lepton_fpa_temperature() {
int lepton_fpa_temperature() {
	if(!_connected) {
		lepton_connect();
	}
	result = LEP_GetSysFpaTemperatureKelvin(&_port, &fpa_temp_kelvin);
	return int(fpa_temp_kelvin);
}

//presumably more commands could go here if desired

void lepton_reboot() {
	if(!_connected) {
		lepton_connect();
	}
	LEP_RunOemReboot(&_port);
}
