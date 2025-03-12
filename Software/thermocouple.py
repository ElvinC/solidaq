# T type thermocouple test
def TemperatureToVoltage(T):
	coeff = []
	voltage = 0

	if T < -270 or T > 400:
		raise ValueError("Temperature out of range")
	elif T < 0:
		coeff = [0.00E+00, 3.87E-02, 4.42E-05, 1.18E-07, 2.00E-08, 9.01E-10, 2.27E-11, 3.61E-13, 3.85E-15, 2.82E-17, 1.43E-19, 4.88E-22, 1.08E-24, 1.39E-27, 7.98E-31]
	else:
		coeff = [0.00E+00, 3.87E-02, 3.33E-05, 2.06E-07, -2.19E-09, 1.10E-11, -3.08E-14, 4.55E-17, -2.75E-20]


	for i, c in enumerate(coeff):
		voltage += c * T**i
	
	print(voltage)
	return voltage

def VoltageToTemperature(V):
	coeff = []
	T = 0


	if V < 0:
		coeff = [0.0000000E+00,2.5949192E+01,-2.1316967E-01,7.9018692E-01,4.2527777E-01,1.3304473E-01,2.0241446E-02,1.2668171E-03]
	else:
		coeff = [0.000000E+00,2.592800E+01,-7.602961E-01,4.637791E-02,-2.165394E-03,6.048144E-05,-7.293422E-07,0.000000E+00]


	for i, c in enumerate(coeff):
		T += c * V**i

	if T < -270 or T > 400:
		raise ValueError("Temperature out of range")

	
	return T

if __name__ == "__main__":
	cold_temp_c = 0 # celsius
	hot_voltage = 5.036 # mv 

	cold_voltage = TemperatureToVoltage(cold_temp_c)
	total_voltage = cold_voltage + hot_voltage
	print(total_voltage)
	hot_temp = VoltageToTemperature(total_voltage)
	print(hot_temp)
