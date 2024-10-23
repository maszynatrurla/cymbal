import spidev
import select
import os
import struct
import time

MAX_PWM = 68
MIN_PWM = 25
START_PWM = 40

DEVINPUT="/dev/input/by-path/platform-fd500000.pcie-pci-0000:01:00.0-usb-0:1.4:1.0-event-kbd"

FULL = 0.4
HALF = 0.25
QWART = 0.12
SIXT  = 0.6

NOTES = {
    'G': (1, 45),
    'A': (1, 43),
    'H': (1, 40),
    'C': (1, 38),
    'D': (1, 36),
    'E': (1, 34),
    'F': (2, 48),
    'g': (2, 45),
    'a': (2, 43),
    'h': (2, 41),
    'c': (2, 39),
    'd': (2, 37),
}

class Kuku:
    def __init__(self):
        self.s = spidev.SpiDev()
        self.s.open(0, 0)
        self.s.max_speed_hz = 10000
        self.duty = {1 : START_PWM, 2 : START_PWM, 0xFF : START_PWM}
        
    def close(self):
        self.s.close()
        
    def setPos(self, pos, dev_id=0xFF):
        if pos >= MIN_PWM and pos <= MAX_PWM:
            self.duty[dev_id] = pos
            self._send([dev_id, 0x01, self.duty[dev_id]])
            print("Position now: %d" % self.duty[dev_id])
            
    def getPos(self, dev_id=0xFF):
        return self.duty[dev_id]
            
    def move(self, ds, dev_id=0xFF):
        duty = self.duty[dev_id] + ds
        if duty > MAX_PWM:
            duty = MAX_PWM
        elif duty < MIN_PWM:
            duty = MIN_PWM
        self.setPos(duty, dev_id)
        
    def programId(self, new_dev_id, old_dev_id=0xFF):
        self._send([old_dev_id, 0x05, new_dev_id])
        
    def bing(self, dt, dev_id=0xFF):
        self._send([dev_id, 0x06, 0xFF & dt])
        
    def _send(self, data):
        csum = 0xFF & sum(data)
        ds = [0x69]
        ds.extend(data)
        ds.append(csum)
        self.s.xfer(ds)
        

        
def set_pos_wait(kuku, new_pos, dev_id=0xFF):
    old_pos = kuku.getPos(dev_id)
    kuku.setPos(new_pos, dev_id)
    ds = abs(new_pos - old_pos)
    if ds > 0:
        time.sleep(0.12)
        
def bing_pos(kuku, new_pos, dev_id=0xFF):
    set_pos_wait(kuku, new_pos, dev_id)
    kuku.bing(2, dev_id)
    
def bing_note(kuku, note):
    dev_id, pos = NOTES[note]
    bing_pos(kuku, pos, dev_id)
    
def play_music_sheet(kuku, text):
    delay = 0
    for tok in text.split():
        if len(tok) != 2:
            continue
        print(tok)
        note, size = tok
        dev_id, pos = NOTES[note]
        
        bing_note(kuku, note)
        delay = {'O' : FULL, 'o' : HALF, '.' : QWART, ',' : SIXT}[size]
        time.sleep(delay)
        
        #start = time.time()
        #set_pos_wait(kuku, pos, dev_id)
        #dt = time.time() - start
        #if dt < delay:
        #    time.sleep(delay - dt)
        #time.sleep(.01)
        #kuku.bing(2, dev_id)
        #delay = {'O' : FULL, 'o' : HALF, '.' : QWART, ',' : SIXT}[size]
        
def interact(kuku):
    dev_id = 1
    
    fp = os.open(DEVINPUT, os.O_RDONLY)
    ep = select.epoll()
    ep.register(fp, select.EPOLLIN)
    
    try:
        while True:
            events = ep.poll(1)
            for fno, event in events:
                bs = os.read(fp, 16)
                if len(bs) == 16:
                    kotki = struct.unpack("IIHHI", bs)
                    type = kotki[2]
                    cod = kotki[3]
                    value = kotki[4]
                    #print("%02X %02X %02X" % (type, cod, value))
                    if type == 0x01 and value == 0x01:
                        # A S D F
                        if cod == 0x1E:
                            kuku.move(2, dev_id)
                        elif cod == 0x1F:
                            kuku.move(1, dev_id)
                        elif cod == 0x20:
                            kuku.move(-1, dev_id)
                        elif cod == 0x21:
                            kuku.move(-2, dev_id)
                        elif cod == 0x36: # rshift
                            kuku.bing(2, dev_id)
                            
                        # space
                        elif cod == 0x39: 
                            kuku.bing(2, 0xFF)
                            
                        # tab
                        elif cod == 0x0F:  
                            kuku.setPos(START_PWM, 0xFF)
                            
                        # Q W E R T Y U I O P [ ]
                        elif cod == 0x10:
                            bing_note(kuku, 'G')
                        elif cod == 0x11:
                            bing_note(kuku, 'A')
                        elif cod == 0x12:
                            bing_note(kuku, 'H')
                        elif cod == 0x13:
                            bing_note(kuku, 'C')
                        elif cod == 0x14:
                            bing_note(kuku, 'D')
                        elif cod == 0x15:
                            bing_note(kuku, 'E')
                        elif cod == 0x16:
                            bing_note(kuku, 'F')
                        elif cod == 0x17:
                            bing_note(kuku, 'g')
                        elif cod == 0x18:
                            bing_note(kuku, 'a')
                        elif cod == 0x19:
                            bing_note(kuku, 'h')
                        elif cod == 0x1A:
                            bing_note(kuku, 'c')
                        elif cod == 0x1B:
                            bing_note(kuku, 'd')
                            
                        elif cod == 0x2A: #lshift
                            if dev_id == 1: dev_id = 2
                            else: dev_id = 1
                            
                        elif cod == 0x60: # right enter
                            set_pos_wait(kuku, 45)
                            kuku.bing(255)
                            time.sleep(.2)
                            set_pos_wait(kuku, 34)
                            time.sleep(.2)
                            kuku.bing(0)
                            
                            
                        else:
                            print("%02X" % cod)
                            
                        #elif cod == 0x10:
                        #    bing_pos(kuku, 53, dev_id)
                        #elif cod == 0x11:
                        #    bing_pos(kuku, 50, dev_id)
                        #elif cod == 0x12:
                        #    bing_pos(kuku, 48, dev_id)
                        #elif cod == 0x13:
                        #    bing_pos(kuku, 46, dev_id)
                        #elif cod == 0x14:
                        #    bing_pos(kuku, 44, dev_id)
                        #elif cod == 0x15:
                        #    bing_pos(kuku, 41, dev_id)
                        #elif cod == 0x16:
                        #    bing_pos(kuku, 39, dev_id)
                        #elif cod == 0x17:
                        #    bing_pos(kuku, 37, dev_id)
                        
                        
    except KeyboardInterrupt:
        pass
        
    kuku.close()
    ep.close()
    os.close(fp)
    
if __name__ == "__main__":
    from sys import argv
    
    kuku = Kuku()
    if len(argv) > 1:
        with open(argv[1]) as fp:
            text = fp.read()
        play_music_sheet(kuku, text)
    
    interact(kuku)
