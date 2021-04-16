from time import sleep
import argparse
from pyftdi.i2c import I2cController, I2cNackError
from os import environ

MDATA_NAME = 0
MDATA_TCA_BYTE = 1 
MDATA_RESET = 2
MDATA_POWER = 3

#       [['modem number', tca6416 byte, reset bit, power bit], [...]]
mData = [['MODEM 1', 0, 0, 1],
        ['MODEM 2', 0, 2, 3],
        ['MODEM 3', 0, 4, 5],
        ['MODEM 4', 0, 6, 7],
        ['MODEM 5', 1, 7, 6],
        ['MODEM 6', 1, 5, 4],
        ['MODEM 7', 1, 3, 2],
        ['MODEM 8', 1, 1, 0]]

FIRST_MODEM_NUM = 0
LAST_MODEM_NUM = 7
    
TCA6416_I2C_ADDR = 0x20
TCA6416_OUTPUT_PORT_0 = 0x02
TCA6416_OUTPUT_PORT_1 = 0x03
TCA6416_CONFIGURATION_0 = 0x06
TCA6416_CONFIGURATION_1 = 0x07
TCA6416_CONF_AS_OUTPUTS = 0x00

ONE_BYTE = 1
TWO_BYTES = 2

class smsDevice:
    i2c = None
    slave = None

    def __init__(self):
        global i2c
        i2c = I2cController()
        self._gpio = GpioController()
        self._state = 0  # SW cache of the GPIO output lines
        global slave
        slave = i2c.get_port(TCA6416_I2C_ADDR)

    def __del__(self):
        i2c.terminate()
        self._gpio.close()

    def i2cInit(self):
        url = environ.get('FTDI_DEVICE', 'ftdi://ftdi:232h/1')
        i2c.set_retry_count(5)
        i2c.configure(url)

        out_pins &= 0xFF
        self._gpio.open_from_url(url, direction=out_pins)

    def readModemStatus(self):
        outState = slave.read_from(TCA6416_OUTPUT_PORT_0, TWO_BYTES)
        data = outState[1] << 8 | outState[0]
        #print("{:016b}".format(data))
        print("+--MODEM--+-POWER STATE-+-RESET STATE-+")
        for x in range(FIRST_MODEM_NUM, LAST_MODEM_NUM + 1, 1):
            print("| MODEM", x+1, "| DEVICE ON   |" if outState[mData[x][MDATA_TCA_BYTE]] 
                    & 1<<mData[x][MDATA_POWER] else "| DEVICE OFF  |","  INACTIVE  |" 
                    if outState[mData[x][MDATA_TCA_BYTE]] & 1<< mData[x][MDATA_RESET] else "   ACTIVE   |")
        print("+---------+-------------+-------------+")
    
    def setPowerState(self, dev):
        temp = list(map(int, dev.deviceNum))
        temp.sort(key=int)
        outState = slave.read_from(TCA6416_OUTPUT_PORT_0, TWO_BYTES)
        print("modem(s)" ,temp, ": turn device(s)", "ON" if int(dev.powerState) == 1 else "OFF")
        
        for x in temp:  
            if int(dev.powerState) == 1:
                outState[mData[x-1][MDATA_TCA_BYTE]] |= 1 << mData[x-1][MDATA_POWER]
            else:
                outState[mData[x-1][MDATA_TCA_BYTE]] &= ~(1 << mData[x-1][MDATA_POWER])

        sleep(0.1)
        slave.write_to(TCA6416_OUTPUT_PORT_0, [outState[0], outState[1]])

    def setResetState(self, dev):
        temp = list(map(int, dev.deviceNum))
        temp.sort(key=int)
        outState = slave.read_from(TCA6416_OUTPUT_PORT_0, TWO_BYTES)
        print("modem(s)", temp, ":devices(s) reset", "ACTIVE" if int(dev.resetState)==1 else "INACTIVE")

        for x in temp:
            if int(dev.resetState) == 0:
                outState[mData[x-1][MDATA_TCA_BYTE]] |= 1 << mData[x-1][MDATA_RESET]
            else:
                outState[mData[x-1][MDATA_TCA_BYTE]] &= ~(1 << mData[x-1][MDATA_RESET])
        
        sleep(0.1)
        slave.write_to(TCA6416_OUTPUT_PORT_0, [outState[0], outState[1]])

    def gpioExpanderConf(self):
        actConf = slave.read_from(TCA6416_CONFIGURATION_0, TWO_BYTES)
        if actConf[0] != 0x00 and actConf[1] != 0x00:
            slave.write_to(TCA6416_CONFIGURATION_0, [TCA6416_CONF_AS_OUTPUTS, TCA6416_CONF_AS_OUTPUTS]) #all IO as outputs
            sleep(0.5)

    def createParser(self):    
        parser = argparse.ArgumentParser(description='example command: smsDevice.py -d 1 3 8 4 -p 1 -r 0',add_help=True)
        parser.add_argument('-d','--device', dest = 'deviceNum', nargs = '*', choices=['1','2','3','4','5','6','7','8']
                , help='Select modem numbers (multiple choose is allowed)', required=False)
        parser.add_argument('-p','--power', dest = 'powerState', choices=['0','1'], help='Set power state',required=False)
        parser.add_argument('-r','--reset', dest = 'resetState', choices=['0','1'], help='Set reset state',required=False)
        parser.add_argument('-s','--status', dest = 'modemStatus', action='store_true', help='Shows modems status', required=False)
        args = parser.parse_args()
        return args

    def parseUserInput(self, devInfo):
        if devInfo.powerState is not None:
            self.setPowerState(devInfo)
        if devInfo.resetState is not None:
            self.setResetState(devInfo)
        self.readModemStatus()
        

def main():
    smsDev = smsDevice()
    smsDev.i2cInit()
    smsDev.gpioExpanderConf()
    userInput = smsDev.createParser()
    smsDev.parseUserInput(userInput)
    del smsDev

if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        print("Error occured: ", str(e))

