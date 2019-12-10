
PROJ := FF_Test

SUBDIRS += src

.PHONY: all clean images flash start serial

ifneq ($(RULES_MK),y)

.DEFAULT_GOAL := all
export ROOT := $(CURDIR)

all:
	$(MAKE) -f $(ROOT)/Rules.mk all

clean:
	rm -rf *.hex *.dfu *.html images
	$(MAKE) -f $(ROOT)/Rules.mk $@

else

all:
	$(MAKE) -C src -f $(ROOT)/Rules.mk $(PROJ).elf $(PROJ).bin $(PROJ).hex
	cp -f src/$(PROJ).hex FF.hex

endif

images:
	rm -rf images
	mkdir -p images
	dd if=/dev/zero of=images/200k.ssd bs=1024 count=200
	dd if=/dev/zero of=images/720k.img bs=1024 count=720
	dd if=/dev/zero of=images/2m88.img bs=1024 count=2880
	dd if=/dev/zero of=images/amiga_1760.adf bs=1024 count=1760
	dd if=/dev/zero of=images/amiga_880.adf bs=1024 count=880
	dd if=/dev/zero of=images/8k.8k.img bs=1024 count=8
	cp scripts/IMG.CFG images/
	cp scripts/FF.CFG images/
	python3 ./scripts/mk_hfe.py --rate 500 images/hd.hfe
	python3 ./scripts/mk_edsk.py images/tst_dsk.dsk
	python3 ./scripts/mk_qd.py --window=2.0 --total=3.2 images/blank.qd 

write: images
	sudo mount /dev/sdd1 /mnt
	sudo rm -rf /mnt/*
	sudo cp -r images/* /mnt/
	sudo umount /mnt

BAUD=115200
DEV=/dev/ttyUSB1

flash: all
	sudo stm32flash -b $(BAUD) -w FF.hex $(DEV)

start:
	sudo stm32flash -b $(BAUD) -g 0 $(DEV)

serial:
	sudo miniterm.py $(DEV) 3000000
